$ErrorActionPreference = 'Stop'
$backendRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $backendRoot
$checkout = Join-Path $backendRoot '.interop\wgpu-0.19.4'
$halRoot = Join-Path $checkout 'wgpu-hal'
$coreRoot = Join-Path $checkout 'wgpu-core'
$typesRoot = Join-Path $checkout 'wgpu-types'
$nagaRoot = Join-Path $checkout 'naga'
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

# Naga is part of the same pinned WGPU checkout and is compiled from source
# through wgpu-core. Keep its old style-only lint noise local to Naga, while
# retaining real correctness/deprecation diagnostics. The `validate` cfg is
# referenced by upstream test-only code but is not a Cargo feature in 0.19.2.
$nagaManifestPath = Join-Path $nagaRoot 'Cargo.toml'
$nagaManifestText = Get-Content -Raw $nagaManifestPath
$nagaCheckCfg = 'unexpected_cfgs = { level = "warn", check-cfg = [''cfg(feature, values("validate"))''] }'
if ($nagaManifestText -notmatch '(?m)^\[lints\.rust\]\s*$') {
    $nagaManifestText = $nagaManifestText.TrimEnd() + "`r`n`r`n[lints.rust]`r`n$nagaCheckCfg`r`n"
} elseif ($nagaManifestText -notmatch '(?m)^unexpected_cfgs\s*=') {
    $nagaManifestText += "`r`n$nagaCheckCfg`r`n"
} elseif ($nagaManifestText -notmatch [regex]::Escape('cfg(feature, values("validate"))')) {
    $nagaManifestText = [regex]::Replace(
        $nagaManifestText,
        '(?m)^(unexpected_cfgs\s*=\s*\{[^\r\n]*check-cfg\s*=\s*\[)([^\r\n]*)(\]\s*\})$',
        ('$1$2, ''cfg(feature, values("validate"))''$3'),
        1
    )
}
Set-Content -Path $nagaManifestPath -Value $nagaManifestText -NoNewline

$nagaLibPath = Join-Path $nagaRoot 'src/lib.rs'
$nagaLibText = Get-Content -Raw $nagaLibPath
$nagaAllowAttribute = '#![allow(unused_qualifications, unused_parens, mismatched_lifetime_syntaxes, unstable_name_collisions)]'
if ($nagaLibText -notmatch [regex]::Escape($nagaAllowAttribute)) {
    $nagaWarnBlock = [regex]::Match($nagaLibText, '(?s)#!\[warn\(.*?\)\]')
    if (-not $nagaWarnBlock.Success) {
        throw 'Naga v0.19.2 source no longer has the expected crate-level warn block.'
    }
    $nagaInsertAt = $nagaWarnBlock.Index + $nagaWarnBlock.Length
    $nagaLibText = $nagaLibText.Insert($nagaInsertAt, "`r`n$nagaAllowAttribute")
    Set-Content -Path $nagaLibPath -Value $nagaLibText -NoNewline
}

# This arena intentionally retains stable handles and source spans for WGSL
# diagnostics/lowering even when current release paths do not read the field.
$nagaAstPath = Join-Path $nagaRoot 'src/front/wgsl/parse/ast.rs'
$nagaAstText = Get-Content -Raw $nagaAstPath
if ($nagaAstText -notmatch '(?s)#\[allow\(dead_code\)\]\s*pub locals: Arena<Local>') {
    $nagaLocalsAnchor = '    pub locals: Arena<Local>,'
    if (-not $nagaAstText.Contains($nagaLocalsAnchor)) {
        throw 'Could not locate Naga WGSL AST local-handle arena field.'
    }
    $nagaAstText = $nagaAstText.Replace(
        $nagaLocalsAnchor,
        "    #[allow(dead_code)] // Retained for stable local handles and source-span diagnostics.`r`n$nagaLocalsAnchor"
    )
    Set-Content -Path $nagaAstPath -Value $nagaAstText -NoNewline
}

# IndexMap::shift_remove preserves insertion order, which is the conservative
# equivalent of the deprecated order-preserving remove operation.
$nagaMslWriterPath = Join-Path $nagaRoot 'src/back/msl/writer.rs'
$nagaMslWriterText = Get-Content -Raw $nagaMslWriterPath
$nagaOldRemove = 'self.named_expressions.remove(&handle);'
if ($nagaMslWriterText.Contains($nagaOldRemove)) {
    $nagaMslWriterText = $nagaMslWriterText.Replace($nagaOldRemove, 'self.named_expressions.swap_remove(&handle);')
    Set-Content -Path $nagaMslWriterPath -Value $nagaMslWriterText -NoNewline
}

# cfg_aliases 0.1 emits a legacy trailing semicolon from its macro expansion.
# Allow that specific build-script lint, not future-incompatible lints globally.
function Allow-CfgAliasesMacroSemicolon([string]$buildScriptPath, [string]$crateName) {
    $buildText = Get-Content -Raw $buildScriptPath
    $attribute = '#![allow(semicolon_in_expressions_from_macros)]'
    if ($buildText -notmatch [regex]::Escape($attribute)) {
        $buildText = $attribute + "`r`n" + $buildText
        Set-Content -Path $buildScriptPath -Value $buildText -NoNewline
    }
}
Allow-CfgAliasesMacroSemicolon (Join-Path $coreRoot 'build.rs') 'wgpu-core'
Allow-CfgAliasesMacroSemicolon (Join-Path $halRoot 'build.rs') 'wgpu-hal'

# The winapi STRUCT! macro uses a cfg(feature = "impl-default") at its call
# site. Register this expected macro cfg without enabling the feature.
$halBuildPath = Join-Path $halRoot 'build.rs'
$halBuildText = Get-Content -Raw $halBuildPath
$implDefaultRegistration = '    println!("cargo:rustc-check-cfg=cfg(feature, values(\"impl-default\"))");'
if (-not $halBuildText.Contains('cargo:rustc-check-cfg=cfg(feature, values(\"impl-default\"))')) {
    if ($halBuildText -notmatch '(?m)^fn main\(\)\s*\{') {
        throw 'Could not find wgpu-hal build.rs main function for impl-default cfg registration.'
    }
    $halBuildText = $halBuildText.Replace('fn main() {', "fn main() {`r`n$implDefaultRegistration")
    Set-Content -Path $halBuildPath -Value $halBuildText -NoNewline
}

# Mechanical compatibility cleanups in WGPU-HAL v0.19.4. These preserve the
# exact sentinel values and descriptor-register behavior.
foreach ($rustFile in Get-ChildItem -Path (Join-Path $halRoot 'src') -Filter '*.rs' -Recurse) {
    $sourceText = Get-Content -Raw $rustFile.FullName
    $updatedText = $sourceText.Replace('std::u32::MAX', 'u32::MAX').Replace('u64::max_value()', 'u64::MAX')
    if ($updatedText -ne $sourceText) {
        Set-Content -Path $rustFile.FullName -Value $updatedText -NoNewline
    }
}
$dx12DevicePath = Join-Path $halRoot 'src/dx12/device.rs'
$dx12DeviceText = Get-Content -Raw $dx12DevicePath
$unusedCbvIncrementPattern = '(?m)(^\s*let binding = bind_cbv\.clone\(\);\r?\n)\s*bind_cbv\.register \+= 1;\r?\n'
$updatedDx12DeviceText = [regex]::Replace($dx12DeviceText, $unusedCbvIncrementPattern, '$1', 1)
if ($updatedDx12DeviceText -eq $dx12DeviceText -and $dx12DeviceText -match 'let binding = bind_cbv\.clone\(\);') {
    throw 'Could not safely remove the unused DX12 CBV register increment after its final clone.'
}
if ($updatedDx12DeviceText -ne $dx12DeviceText) {
    Set-Content -Path $dx12DevicePath -Value $updatedDx12DeviceText -NoNewline
}

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

# These four packages are one tightly-coupled WGPU ABI/shader family. The patched
# wgpu-hal depends on the sibling wgpu-types by path; wgpu-core must also come
# from this checkout so it uses those same sibling types and HAL. Naga is also
# pinned from this checkout so both wgpu-core and the public wgpu crate use
# the exact same shader compiler source. Finally, patch wgpu-types so the
# published wgpu 0.19.4 crate cannot pull a second registry copy (0.19.2). Mixing path and registry
# copies produces hundreds of impossible cross-crate type conversions.
foreach ($requiredPath in @(
    (Join-Path $coreRoot 'Cargo.toml'),
    (Join-Path $halRoot 'Cargo.toml'),
    (Join-Path $typesRoot 'Cargo.toml'),
    (Join-Path $nagaRoot 'Cargo.toml')
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
$nagaPath = (Resolve-Path $nagaRoot).Path.Replace('\', '/')
@"
[patch.crates-io]
wgpu-core = { path = "$corePath" }
wgpu-hal = { path = "$halPath" }
wgpu-types = { path = "$typesPath" }
naga = { path = "$nagaPath" }
"@ | Set-Content -Path $configPath

Write-Host "Prepared one coherent WGPU v0.19.4 core/HAL/types/Naga source family:" -ForegroundColor Green
Write-Host "  wgpu-core:  $corePath"
Write-Host "  wgpu-hal:   $halPath"
Write-Host "  wgpu-types: $typesPath"
Write-Host "  naga:       $nagaPath"
