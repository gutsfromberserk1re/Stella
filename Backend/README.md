# Stella Rust backend

The native raylib interface talks to the Rust `pluto-backend` C ABI DLL. There is no Tauri or WebView in the application.

## Modules

- `timeline-core/` owns clip data, validation, snapping, and timeline mutations.
- `backend-ffi/` exports a narrow C ABI. Cheap timeline commands are synchronous; media jobs, preview decoding, and proxies use separate background workers and are collected with `pluto_backend_poll` so rendering stays responsive.
- `Frontend/backend_bridge.h` loads the DLL dynamically. GitHub Actions builds the C UI and Rust DLL together and publishes a lightweight artifact plus a full FFmpeg-bundled artifact.

The FFI accepts and returns UTF-8 JSON strings. Strings returned by the backend must be released with `pluto_backend_free`. See the exported function signatures in `Frontend/backend_bridge.h` and `Backend/backend-ffi/src/lib.rs`.

## Implemented engine paths

- Timeline get/add/move/split/delete, snapping, undo/redo, clip properties, and keyframe storage run synchronously in Rust.
- Media inspection, FFmpeg thumbnails, audio waveform generation, codec decoding, and export are handled by the media worker. Preview requests one decoded RGBA source frame from FFmpeg at the requested timestamp; FFmpeg is not asked to scale, composite, or apply preview effects.
- The preview worker uploads the decoded frame to a wgpu texture, renders an aspect-fit pass with Stella's WGSL shader into an offscreen RGBA target, and reads that rendered target back through a staging buffer. The existing C ABI/Raylib preview surface currently displays that GPU-rendered image.
- The initial native wgpu preview pass handles source-frame upload and aspect-fit/letterbox rendering. Multi-track GPU compositing, transforms, opacity, effects, and transitions are the next renderer stages and are not implemented in this pass.
- The current export implementation is still FFmpeg filter-graph based; it has not yet been migrated to a wgpu frame renderer feeding FFmpeg encoding. That remaining export path is a known architecture gap relative to the intended codec-only FFmpeg role.
- Playback timing traces are emitted to the Windows debugger once per second with end-to-end, queue, decode wait, GPU render/readback, texture upload, and backend render times where available.

FFmpeg and FFprobe resolve from `PLUTO_FFMPEG_DIR`, the app's adjacent `ffmpeg/` folder, or `PATH`. `Stella-Windows-x64-lite` contains `Stella.exe`, `pluto_backend.dll`, and icons. `Stella-Windows-x64-full` also contains FFmpeg/FFprobe, their shared runtime DLLs, and notices. The lightweight build can use the `ffmpeg/` folder from an earlier full download by placing it beside `Stella.exe`.

Video effects currently use the FFmpeg filter builder for export only; the preview path intentionally does not invoke FFmpeg filters. A transition is stored on the outgoing clip at `props.transitionOut` with a type and duration; it is used only when a neighboring video clip follows directly on the same track. The renderer overlaps the incoming clip by that duration and composites it over the outgoing clip.

Timeline scrubbing and left/right frame stepping use the current 30 fps timebase. The persistent stream path is newly integrated and still needs runtime verification from a GitHub build on representative media and machines.

## Native wgpu preview compositing

Preview rendering now composites all active video clips on video tracks V1 through V4 in bottom-to-top order using a wgpu render pass. FFmpeg remains the source-frame decoder only. Per-clip `props.transform.opacity` is applied in the GPU fragment shader; alpha blending is enabled in the render pipeline, and each layer is aspect-fit with transparent letterbox regions so upper layers do not cover lower layers with black bars. The final offscreen image is still read back into the existing UI preview buffer (the next architectural step is a direct GPU-backed presentation surface).
