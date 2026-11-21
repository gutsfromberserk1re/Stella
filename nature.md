# Nature

## Setup
- Native C/raylib UI connects to the Rust C ABI; no Tauri or WebView.
- Preview decoding is in-process through `ffmpeg-next` and static decode-only FFmpeg libraries. FFmpeg CLI is reserved for import/probe, thumbnails, waveforms, proxy generation and final export jobs; it is not launched for each preview frame.
- wgpu owns the preview compositor and WGSL visual processing. Raylib still displays the copied final frame until the preview window moves to a direct wgpu surface.

## Done
- Workflow builds lite and FFmpeg-bundled versions and builds the native decoder dependency.
- FFmpeg import/inspection, regenerated thumbnail and waveform caches, proxy and final export jobs.
- Timeline snapping, linked audio, grouping, drag previews, frame stepping, keyframe markers and undo/redo.
- Shift+K cuts at the playhead; the scissors tool cuts at the pointer.
- Preview has 24/30/60 fps target controls; library-only low-res thumbnails and cached waveforms.
- Timeline swatches stay consistent across cuts and grouped clips; video audio bands and audio-track waveforms are visible.
- Grouped clips move together with offsets preserved, share effects/colors, and can be ungrouped.
- Color wheels, HSL/light/color/detail controls, presets and visual effect controls are stored on clip properties.
- Transitions are stored on outgoing clips and are composited with their incoming clips.
- Persistent libav decoder contexts per source replace per-preview-frame ffmpeg/ffprobe process launches.
- Bounded decoded-frame cache keyed by media path and nominal source-frame index; common RGB/BGRA, planar 8-bit 4:2:0/4:2:2/4:4:4, planar 9/10/12/16-bit LE YUV, and P010LE conversions are covered.
- Immutable Arc timeline snapshots keep full-project JSON cloning/serialization off the per-frame preview request path.
- Persistent wgpu source textures, uniform buffers, bind groups and render target; per-layer WGSL compositing for V1-V4.
- Shader-side transform, crop, opacity, base color adjustments, HSL, color wheels, presets and a first set of real-time video effects/transitions. Preview shader effects currently approximate the export graph; they are not yet pixel-identical.
- Preview stale-frame rejection no longer uses the former fixed 200ms threshold; frames are accepted in the current playback/scrub direction.

## Remaining
- Build and runtime verification on Windows; Rust/Cargo are not available in this editing environment.
- Expand decoded pixel-format/colorimetry coverage and validate 10-bit/12-bit, HDR, ProRes and AV1 sources; current planar conversion assumes limited-range BT.601 coefficients and does not perform HDR transfer/tone mapping.
- Add decode-ahead scheduling, replace single-flight preview requests with a latest-frame/pipelined contract, and improve variable-frame-rate frame lookup.
- Complete shader parity for every export filter (including detailed vignette, light leak, effect variants and all transition directions).
- Replace wgpu readback + Raylib texture upload with direct wgpu-surface presentation, removing the CPU round-trip.
- Migrate export visual compositing/effects to the wgpu engine so FFmpeg is used only to decode/encode/audio across the entire application.
- Synchronize playback to the audio clock and implement shared keyframe interpolation.
