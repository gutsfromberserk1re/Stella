# HSL All-Bands Rendering Fix

## What changed

- The active HSL swatch is now inspector state only; it no longer selects which HSL adjustment reaches the renderer.
- The renderer uniform carries the global `All` band plus all eight colour bands in one update.
- Preview applies every stored HSL band during each frame. Hue masks are calculated from the same pre-HSL source colour, so a prior hue adjustment cannot move pixels into another band's mask mid-pass.
- Overlapping hue masks blend their adjustments instead of repeatedly applying stacked per-band transforms.
- The legacy FFmpeg filter-graph path also emits a filter for every non-neutral HSL band rather than reading only `selected`.
- Added tests verifying all nine HSL bands reach preview uniforms regardless of selected swatch, and multiple bands are included in the export filter graph.

## Expected behavior

Changing the selected colour circle changes only which band's Hue/Saturation/Lightness controls are being edited. All adjustments already made to other circles remain visible in the preview and export at the same time.

## Verification note

This environment does not have Cargo/Rust installed, so Rust tests and the native WGPU shader compilation could not be run here. ZIP integrity and source-level consistency checks were run on the packaged project.
