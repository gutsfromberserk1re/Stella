$ErrorActionPreference = 'Stop'
$backendRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $backendRoot
$checkout = Join-Path $backendRoot '.interop\wgpu-0.19.4'
$halRoot = Join-Path $checkout 'wgpu-hal'
$coreRoot = Join-Path $checkout 'wgpu-core'
$typesRoot = Join-Path $checkout 'wgpu-types'
$configDir = Join-Path $repoRoot '.cargo'
$configPath = Join-Path $configDir 'config.toml'

if (-not (Test-Path (Join-Path $halRoot 'src/gles/wgl.rs'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $checkout) | Out-Null
    if (Test-Path $checkout) { Remove-Item -LiteralPath $checkout -Recurse -Force }
    git clone --depth 1 --branch v0.19.4 https://github.com/gfx-rs/wgpu.git $checkout
    if ($LASTEXITCODE -ne 0) { throw 'Could not fetch the wgpu v0.19.4 source required for the WGL interop accessor patch.' }
}

# wgpu-core and wgpu-hal both inherit cfg_aliases from the upstream WGPU
# workspace. Cargo is resolving these path-patched crates from Stella's
# separate Backend workspace, so make both build dependencies standalone.
# Keep the exact cfg_aliases 0.1 line used by the v0.19.4 source family.
function Set-StandaloneCfgAliases([string]$manifestPath, [string]$crateName) {
    if (-not (Test-Path $manifestPath)) {
        throw "The pinned WGPU checkout is missing $crateName/Cargo.toml."
    }
    $manifestText = Get-Content -Raw $manifestPath
    if ($manifestText -match '(?m)^cfg_aliases\.workspace\s*=\s*true\s*$') {
        $manifestText = [regex]::Replace($manifestText,
            '(?m)^cfg_aliases\.workspace\s*=\s*true\s*$', 'cfg_aliases = "0.1"', 1)
        Set-Content -Path $manifestPath -Value $manifestText -NoNewline
    }
    if ((Get-Content -Raw $manifestPath) -match '(?m)^cfg_aliases\.workspace\s*=\s*true\s*$') {
        throw "Could not make $crateName cfg_aliases independent of workspace inheritance."
    }
}

Set-StandaloneCfgAliases (Join-Path $halRoot 'Cargo.toml') 'wgpu-hal'
Set-StandaloneCfgAliases (Join-Path $coreRoot 'Cargo.toml') 'wgpu-core'

# Keep upstream WGPU warning output useful without changing its implementation.
# These are narrowly scoped legacy-source lints: explicit custom cfg names are
# registered with rustc, while style-only qualifications/lifetime spelling
# warnings are suppressed for the vendored 0.19.4 crates only.
function Set-UpstreamLintHygiene([string]$manifestPath, [string]$crateName) {
    $manifestText = Get-Content -Raw $manifestPath
    $lintTable = @'

[lints.rust]
unexpected_cfgs = { level = "warn", check-cfg = ["cfg(dx12)", "cfg(gles)", "cfg(metal)", "cfg(vulkan)", "cfg(webgl)", "cfg(send_sync)"] }
unused_qualifications = "allow"
mismatched_lifetime_syntaxes = "allow"
'@
    if ($manifestText -notmatch '(?m)^\[lints\.rust\]\s*$') {
        $manifestText = $manifestText.TrimEnd() + $lintTable
        Set-Content -Path $manifestPath -Value $manifestText -NoNewline
    } else {
        # Do not silently override upstream lint policy if the pinned source
        # later grows its own table; require an intentional update instead.
        foreach ($requiredLint in @('unexpected_cfgs', 'unused_qualifications', 'mismatched_lifetime_syntaxes')) {
            if ($manifestText -notmatch "(?m)^$requiredLint\s*=") {
                throw "$crateName already defines [lints.rust] but is missing $requiredLint; update the interop patch explicitly."
            }
        }
    }
}

Set-UpstreamLintHygiene (Join-Path $halRoot 'Cargo.toml') 'wgpu-hal'
Set-UpstreamLintHygiene (Join-Path $coreRoot 'Cargo.toml') 'wgpu-core'

# WGPU v0.19.4 explicitly enables `unused_qualifications` in its crate roots.
# Apply narrowly scoped crate-level allows after those warn attributes so these
# style-only lints do not flood a downstream application build. No implementation
# code, backend selection, synchronization, or validation is changed.
function Set-UpstreamSourceLintHygiene([string]$libPath, [string]$crateName) {
    $sourceText = Get-Content -Raw $libPath
    $allowAttribute = '#![allow(unused_qualifications, mismatched_lifetime_syntaxes)]'
    if ($sourceText -notmatch [regex]::Escape($allowAttribute)) {
        $warnBlock = [regex]::Match($sourceText, '(?s)#!\[warn\(.*?\)\]')
        if (-not $warnBlock.Success) {
            throw "$crateName source no longer has the expected crate-level warn block; update the WGPU lint patch deliberately."
        }
        $insertAt = $warnBlock.Index + $warnBlock.Length
        $sourceText = $sourceText.Insert($insertAt, "`r`n$allowAttribute")
        Set-Content -Path $libPath -Value $sourceText -NoNewline
    }
}

Set-UpstreamSourceLintHygiene (Join-Path $coreRoot 'src/lib.rs') 'wgpu-core'
Set-UpstreamSourceLintHygiene (Join-Path $halRoot 'src/lib.rs') 'wgpu-hal'

# Register WGPU's legacy cfg aliases with modern rustc through each crate's
# build-script output. The cfg_aliases! invocations remain unchanged and still
# compute the actual target/backend predicates.
function Register-UpstreamCfgChecks([string]$buildScriptPath, [string]$crateName) {
    $buildText = Get-Content -Raw $buildScriptPath
    $cfgNames = @('native', 'send_sync', 'webgl', 'Emscripten', 'dx12', 'gles', 'metal', 'vulkan')
    $missingLines = @()
    foreach ($cfgName in $cfgNames) {
        $registration = "cargo:rustc-check-cfg=cfg($cfgName)"
        if ($buildText -notmatch [regex]::Escape($registration)) {
            $missingLines += ('    println!("' + $registration + '");')
        }
    }
    if ($missingLines.Count -gt 0) {
        if ($buildText -notmatch '(?m)^fn main\(\)\s*\{') {
            throw "$crateName build.rs no longer has the expected main function."
        }
        $block = $missingLines -join "`r`n"
        $buildText = $buildText.Replace('fn main() {', "fn main() {`r`n$block")
        Set-Content -Path $buildScriptPath -Value $buildText -NoNewline
    }
}

Register-UpstreamCfgChecks (Join-Path $coreRoot 'build.rs') 'wgpu-core'
Register-UpstreamCfgChecks (Join-Path $halRoot 'build.rs') 'wgpu-hal'

# Replace deprecated primitive MAX/MIN paths in the pinned core source with
# identical associated constants. This is a mechanical warning-only cleanup.
$textureInitPath = Join-Path $coreRoot 'src/init_tracker/texture.rs'
$textureInitText = Get-Content -Raw $textureInitPath
$textureInitText = $textureInitText.Replace('std::usize::MAX', 'usize::MAX')
$textureInitText = $textureInitText.Replace('std::usize::MIN', 'usize::MIN')
$textureInitText = $textureInitText.Replace('std::u32::MAX', 'u32::MAX')
$textureInitText = $textureInitText.Replace('std::u32::MIN', 'u32::MIN')
Set-Content -Path $textureInitPath -Value $textureInitText -NoNewline

# These three packages are one tightly-coupled WGPU ABI family. The patched
# wgpu-hal depends on the sibling wgpu-types by path; wgpu-core must also come
# from this checkout so it uses those same sibling types and HAL. Finally,
# patch wgpu-types itself so the published wgpu 0.19.4 crate cannot pull a
# second registry copy (0.19.2) into the graph. Mixing path and registry
# copies produces hundreds of impossible cross-crate type conversions.
foreach ($requiredPath in @(
    (Join-Path $coreRoot 'Cargo.toml'),
    (Join-Path $halRoot 'Cargo.toml'),
    (Join-Path $typesRoot 'Cargo.toml')
)) {
    if (-not (Test-Path $requiredPath)) {
        throw "Pinned WGPU source family is incomplete; missing $requiredPath."
    }
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
$corePath = (Resolve-Path $coreRoot).Path.Replace('\', '/')
$halPath = (Resolve-Path $halRoot).Path.Replace('\', '/')
$typesPath = (Resolve-Path $typesRoot).Path.Replace('\', '/')
@"
[patch.crates-io]
wgpu-core = { path = "$corePath" }
wgpu-hal = { path = "$halPath" }
wgpu-types = { path = "$typesPath" }
"@ | Set-Content -Path $configPath

Write-Host "Prepared one coherent WGPU v0.19.4 core/HAL/types source family:" -ForegroundColor Green
Write-Host "  wgpu-core:  $corePath"
Write-Host "  wgpu-hal:   $halPath"
Write-Host "  wgpu-types: $typesPath"
