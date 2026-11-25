$ErrorActionPreference = 'Stop'
$backendRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $backendRoot
$checkout = Join-Path $backendRoot '.interop\wgpu-0.19.4'
$halRoot = Join-Path $checkout 'wgpu-hal'
$configDir = Join-Path $repoRoot '.cargo'
$configPath = Join-Path $configDir 'config.toml'

if (-not (Test-Path (Join-Path $halRoot 'src/gles/wgl.rs'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $checkout) | Out-Null
    if (Test-Path $checkout) { Remove-Item -LiteralPath $checkout -Recurse -Force }
    git clone --depth 1 --branch v0.19.4 https://github.com/gfx-rs/wgpu.git $checkout
    if ($LASTEXITCODE -ne 0) { throw 'Could not fetch the wgpu v0.19.4 source required for the WGL interop accessor patch.' }
}

$modPath = Join-Path $halRoot 'src/gles/mod.rs'
$modText = Get-Content -Raw $modPath
$old = '(?m)^#\[cfg\(windows\)\]\r?\nuse self::wgl::AdapterContext;'
$new = "#[cfg(windows)]`r`npub use self::wgl::{AdapterContext, AdapterContextLock, set_host_gl_share_context, set_host_pixel_format_dc};"
if ($modText -notmatch 'pub use self::wgl::\{AdapterContext, AdapterContextLock, set_host_gl_share_context, set_host_pixel_format_dc\}') {
    $modText = [regex]::Replace($modText, $old, $new, 1)
    if ($modText -notmatch 'pub use self::wgl::\{AdapterContext, AdapterContextLock, set_host_gl_share_context, set_host_pixel_format_dc\}') {
        throw 'Could not locate the Windows AdapterContext re-export point in wgpu-hal 0.19.4.'
    }
    Set-Content -Path $modPath -Value $modText -NoNewline
}

$wglPath = Join-Path $halRoot 'src/gles/wgl.rs'
$wglText = Get-Content -Raw $wglPath

# Resolve Raylib's display device for the hidden WGPU window.
if ($wglText -notmatch 'WindowFromDC') {
    $wglText = $wglText.Replace('windef::{HDC, HGLRC, HWND},', 'windef::{HDC, HGLRC, HWND, RECT},')
    $wglText = $wglText.Replace('CreateWindowExA, DefWindowProcA, DestroyWindow, GetDC, RegisterClassExA, ReleaseDC,', 'CreateWindowExA, DefWindowProcA, DestroyWindow, GetDC, GetWindowRect, RegisterClassExA, ReleaseDC, WindowFromDC,')
    if ($wglText -notmatch 'windef::\{HDC, HGLRC, HWND, RECT\}') { throw 'Could not add the WGL RECT import needed for monitor matching.' }
    if ($wglText -notmatch 'WindowFromDC') { throw 'Could not add WGL window-location imports.' }
}

# Register the host window DC before WGPU creates its hidden WGL window.
$hostDcAnchor = 'const CONTEXT_LOCK_TIMEOUT_SECS: u64 = 1;'
$hostDcInsert = @'
const CONTEXT_LOCK_TIMEOUT_SECS: u64 = 1;

// Pixel format copied from the host Raylib window before the WGPU hidden
// window chooses one. WGL sharing requires compatible context pixel formats.
static HOST_PIXEL_FORMAT_DC: std::sync::atomic::AtomicUsize =
    std::sync::atomic::AtomicUsize::new(0);

pub fn set_host_pixel_format_dc(dc: *mut c_void) {
    HOST_PIXEL_FORMAT_DC.store(dc as usize, std::sync::atomic::Ordering::Release);
}
'@
if ($wglText -notmatch 'pub fn set_host_pixel_format_dc\(') {
    if (-not $wglText.Contains($hostDcAnchor)) { throw 'Could not locate WGL context-lock constant in wgpu-hal 0.19.4.' }
    $wglText = $wglText.Replace($hostDcAnchor, $hostDcInsert.TrimEnd())
}

# Reuse the host's exact pixel-format index/descriptor where possible. The
# normal WGPU chooser remains the fallback for non-hosted test instances.
$oldPixelChooserPattern = '(?s)    let index = unsafe \{ ChoosePixelFormat\(dc, &format\) \};\s+if index == 0 \{\s+return Err\(crate::InstanceError::with_source\(\s+String::from\("unable to choose pixel format"\),\s+Error::last_os_error\(\),\s+\)\);\s+\}\s*'
$newPixelChooser = @'
    let host_dc = HOST_PIXEL_FORMAT_DC.load(std::sync::atomic::Ordering::Acquire) as HDC;
    let host_index = if host_dc.is_null() { 0 } else { unsafe { GetPixelFormat(host_dc) } };
    let index = if host_index != 0 {
        if unsafe { DescribePixelFormat(host_dc, host_index, mem::size_of_val(&format) as UINT, &mut format) } == 0 {
            return Err(crate::InstanceError::with_source(
                String::from("unable to describe host OpenGL pixel format"),
                Error::last_os_error(),
            ));
        }
        host_index
    } else {
        unsafe { ChoosePixelFormat(dc, &format) }
    };
    if index == 0 {
        return Err(crate::InstanceError::with_source(
            String::from("unable to choose pixel format"),
            Error::last_os_error(),
        ));
    }
'@
if ($wglText -notmatch 'HOST_PIXEL_FORMAT_DC\.load') {
    $patchedWglText = [regex]::Replace($wglText, $oldPixelChooserPattern, $newPixelChooser.TrimEnd(), 1)
    if ($patchedWglText -eq $wglText) { throw 'Could not locate WGL pixel-format chooser in wgpu-hal 0.19.4.' }
    $wglText = $patchedWglText
}

# Keep WGPU's hidden context window on Raylib's display device.
if ($wglText -notmatch 'let host_window = if host_dc\.is_null\(\)') {
    $windowCreatePattern = '(?s)                // Create a hidden window since we don.t pass `WS_VISIBLE`\.\s+let window = unsafe \{\s+CreateWindowExA\(\s+0,\s+window_class\.as_ptr\(\),\s+window_class\.as_ptr\(\),\s+0,\s+0,\s+0,\s+1,\s+1,'
    $windowCreateReplacement = @'
                // Create the hidden WGPU window on Raylib's display. WGL
                // contexts on different display devices cannot share objects.
                // The window remains invisible (no WS_VISIBLE style).
                let host_dc = HOST_PIXEL_FORMAT_DC.load(
                    std::sync::atomic::Ordering::Acquire,
                ) as HDC;
                let host_window = if host_dc.is_null() {
                    ptr::null_mut()
                } else {
                    unsafe { WindowFromDC(host_dc) }
                };
                let mut host_rect: RECT = unsafe { mem::zeroed() };
                let (window_x, window_y) = if !host_window.is_null()
                    && unsafe { GetWindowRect(host_window, &mut host_rect) } != 0
                {
                    (host_rect.left, host_rect.top)
                } else {
                    (0, 0)
                };
                let window = unsafe {
                    CreateWindowExA(
                        0,
                        window_class.as_ptr(),
                        window_class.as_ptr(),
                        0,
                        window_x,
                        window_y,
                        1,
                        1,
'@
    $patchedWglText = [regex]::Replace($wglText, $windowCreatePattern, $windowCreateReplacement.TrimEnd(), 1)
    if ($patchedWglText -eq $wglText) { throw 'Could not locate WGL hidden-window creation point in wgpu-hal 0.19.4.' }
    $wglText = $patchedWglText
}
if ($wglText -notmatch '#\[derive\(Clone\)\]\s*pub struct AdapterContext') {
    $wglText = $wglText.Replace('pub struct AdapterContext {', "#[derive(Clone)]`r`npub struct AdapterContext {")
}
if ($wglText -notmatch '#\[derive\(Clone\)\]\s*pub struct AdapterContext') {
    throw 'Could not add Clone to the WGL AdapterContext wrapper.'
}
# Join the share group as part of WGL context creation. Calling wglShareLists
# after Raylib has already created textures/VAOs can fail on conforming drivers.
if ($wglText -notmatch 'pub fn set_host_gl_share_context\(') {
    $pixelSetterPattern = '(?s)(pub fn set_host_pixel_format_dc\(dc: \*mut c_void\) \{.*?\n\})'
    $pixelSetterInsert = @'

// Create the WGPU WGL context in the same share group as Raylib's context.
static HOST_GL_SHARE_CONTEXT: std::sync::atomic::AtomicUsize =
    std::sync::atomic::AtomicUsize::new(0);

pub fn set_host_gl_share_context(context: *mut c_void) {
    HOST_GL_SHARE_CONTEXT.store(context as usize, std::sync::atomic::Ordering::Release);
}
'@
    if ($wglText -notmatch $pixelSetterPattern) { throw 'Could not locate the WGL host pixel-format setter.' }
    $wglText = [regex]::Replace($wglText, $pixelSetterPattern, ('$1' + $pixelSetterInsert), 1)
}

$oldCreateContextCall = 'extra.CreateContextAttribsARB(dc as *const _, ptr::null(), attributes.as_ptr())'
$newCreateContextCall = @'
{
                let host_share_context = HOST_GL_SHARE_CONTEXT.load(std::sync::atomic::Ordering::Acquire) as HGLRC;
                extra.CreateContextAttribsARB(dc as *const _, host_share_context as *const c_void, attributes.as_ptr())
            }
'@
if (-not $wglText.Contains('HOST_GL_SHARE_CONTEXT.load')) {
    if (-not $wglText.Contains($oldCreateContextCall)) { throw 'Could not locate WGL context creation call in wgpu-hal 0.19.4.' }
    $wglText = $wglText.Replace($oldCreateContextCall, $newCreateContextCall.Trim())
}

if ($wglText -notmatch 'shared WGL preview requires WGL_ARB_create_context_profile') {
    $legacyBranchPattern = '(?m)^        \} else \{\r?\n            context\r?\n        \};'
    $newLegacyBranch = @'
        } else {
            let host_share_context = HOST_GL_SHARE_CONTEXT.load(std::sync::atomic::Ordering::Acquire) as HGLRC;
            if !host_share_context.is_null() {
                return Err(crate::InstanceError::new(String::from(
                    "shared WGL preview requires WGL_ARB_create_context_profile",
                )));
            }
            context
        };
'@
    $patchedWglText = [regex]::Replace($wglText, $legacyBranchPattern, $newLegacyBranch.TrimEnd(), 1)
    if ($patchedWglText -eq $wglText) { throw 'Could not locate WGL legacy-context fallback branch.' }
    $wglText = $patchedWglText
}
Set-Content -Path $wglPath -Value $wglText -NoNewline

$adapterPath = Join-Path $halRoot 'src/gles/adapter.rs'
$adapterText = Get-Content -Raw $adapterPath
if ($adapterText -notmatch 'pub fn adapter_context\(') {
    $needle = 'impl super::Adapter {'
    $insertion = @'
impl super::Adapter {
    /// Returns a cloneable handle to the WGL context used by this adapter.
    /// The context already joined the host share group in the WGL constructor;
    /// this accessor lets the app queue GLsync handoffs on that context.
    pub fn adapter_context(&self) -> super::AdapterContext {
        self.shared.context.clone()
    }
'@
    if (-not $adapterText.Contains($needle)) { throw 'Could not locate GLES Adapter implementation in wgpu-hal 0.19.4.' }
    $adapterText = $adapterText.Replace($needle, $insertion.TrimEnd())
    Set-Content -Path $adapterPath -Value $adapterText -NoNewline
}

New-Item -ItemType Directory -Force -Path $configDir | Out-Null
$halPath = (Resolve-Path $halRoot).Path.Replace('\', '/')
@"
[patch.crates-io]
wgpu-hal = { path = "$halPath" }
"@ | Set-Content -Path $configPath

Write-Host "Prepared patched wgpu-hal 0.19.4 at $halPath" -ForegroundColor Green
