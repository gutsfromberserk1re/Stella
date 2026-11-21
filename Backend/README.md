# Stella Rust backend

The native raylib interface talks to the Rust `pluto-backend` C ABI DLL. There is no Tauri or WebView in the application.

## Modules

- `timeline-core/` owns clip data, validation, snapping, and timeline mutations.
- `backend-ffi/` exports a narrow C ABI. Cheap timeline commands are synchronous; media jobs, preview decoding, and proxies use separate background workers and are collected with `pluto_backend_poll` so rendering stays responsive.
- `Frontend/backend_bridge.h` loads the DLL dynamically. GitHub Actions builds the C UI and Rust DLL together and publishes a lightweight artifact plus a full FFmpeg-bundled artifact.

The FFI accepts and returns UTF-8 JSON strings. Strings returned by the backend must be released with `pluto_backend_free`. See the exported function signatures in `Frontend/backend_bridge.h` and `Backend/backend-ffi/src/lib.rs`.

## Implemented engine paths

- Timeline get/add/move/split/delete, snapping, undo/redo, clip properties, and keyframe storage run synchronously in Rust.
- Media import/inspection, thumbnail and waveform generation, proxy creation, and final export remain offline/background media jobs. Low-resolution 320×180 media thumbnails are decoded in-process through the same FFmpeg libraries and written to a small BMP cache, so they do not require `ffmpeg.exe`. Waveform generation, video metadata inspection, proxy creation, and final export still use the FFmpeg command-line tools; none of those processes are spawned for preview frames.
- Preview video decoding uses `ffmpeg-next` against the decode-only static FFmpeg libraries. The preview worker keeps decoder contexts open per source, seeks and flushes those contexts when needed, and caches decoded RGBA frames in a bounded LRU. There is no per-frame `ffprobe` or `ffmpeg` process launch. Manual pixel conversion currently covers common 8-bit RGB/BGRA, planar 4:2:0/4:2:2/4:4:4, 9/10/12/16-bit little-endian planar YUV, and P010LE; unusual formats and source colorimetry/HDR handling still need further validation.
- Preview requests carry an immutable `Arc<Timeline>` snapshot created when the timeline changes. The UI does not serialize or clone the project timeline for every playback frame.
- Decoded frames are uploaded into persistent wgpu textures. The WGSL compositor uses per-layer uniforms for position, scale, rotation, crop, opacity, exposure/brightness, contrast, saturation, vibrance, HSL selection, temperature, shadows/midtones/highlights color wheels, presets, and a first set of visual effects/transitions. These shader implementations are native preview approximations, not yet pixel-identical to every FFmpeg export filter. Multiple active video tracks are composited bottom-to-top.
- The current Raylib/OpenGL UI still receives the GPU-composited result through a persistent wgpu readback buffer and CPU texture upload. This is a compatibility bridge, not the final zero-copy presentation path; a native wgpu surface is still a separate future step.
- Preview playback accepts the newest completed frame in the current travel direction and no longer applies the previous arbitrary 200ms stale-frame rejection gate. Requests remain single-flight, so final frame pacing is currently limited by the slowest decode/composite/readback request. Decode-ahead scheduling and an audio-clock playback contract are not implemented yet.
- The current export implementation is still FFmpeg filter-graph based. This is intentionally separate from preview decoding, but it means FFmpeg is not yet restricted to codec operations across the entire application. Migrating export compositing/effects to wgpu and feeding rendered frames to FFmpeg encoding remains outstanding.

The Windows build links the in-process decoder against the LGPL-2.1-or-later decode-only static FFmpeg libraries. `ffmpeg-next`/`ffmpeg-sys-next` generate bindings during the build, so CI installs LLVM/libclang and sets `FFMPEG_DIR`. The full package may also include the separate GPL FFmpeg CLI binaries for import/proxy/export workflows; their license notices are distributed separately. The lite package includes the backend DLL and icons. Image thumbnails can be generated in-process; video import/metadata inspection and operations such as waveform/proxy generation and export still need the `ffmpeg/` folder or FFmpeg on `PATH`.

## Native wgpu preview compositing

The preview renderer caches source textures, uniform buffers, bind groups and the render target instead of recreating them on every frame. It composites active clips on video tracks V1 through V4 in bottom-to-top order. Source frames are decoded in-process; visual processing after decode is shader-side. Color and transform properties update uniforms, so changing a color wheel or inspector value rerenders the cached decoded frame without running an FFmpeg filter graph. A staging-buffer readback is currently necessary because the existing preview window is still a Raylib/OpenGL surface.

FFmpeg command-line tools resolve from `PLUTO_FFMPEG_DIR`, the app's adjacent `ffmpeg/` folder, or `PATH` for video metadata inspection, waveform, proxy, and export tasks. `Stella-Windows-x64-lite` contains `Stella.exe`, `pluto_backend.dll`, and icons. `Stella-Windows-x64-full` also contains FFmpeg/FFprobe, their shared runtime DLLs, and notices. The lightweight build can use the `ffmpeg/` folder from an earlier full download by placing it beside `Stella.exe`.

A transition is stored on the outgoing clip at `props.transitionOut` with a type and duration; it is used only when a neighboring video clip follows directly on the same track. The preview renderer overlaps the incoming clip by that duration and evaluates its transition mask on the GPU. Export still uses the FFmpeg filter graph until the export renderer is migrated.

Timeline scrubbing and left/right frame stepping use the current timeline frame-rate setting. The current preview decoder, cache, color and transform shader paths still need a Windows CI build and runtime verification against representative H.264, HEVC 10-bit, ProRes and AV1 media.
