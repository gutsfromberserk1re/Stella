# Stella Rust backend

The native raylib interface talks to the Rust `pluto-backend` C ABI DLL. There is no Tauri or WebView in the application.

## Modules

- `timeline-core/` owns clip data, validation, snapping, and timeline mutations.
- `backend-ffi/` exports a narrow C ABI. Cheap timeline commands are synchronous; media jobs, preview decoding, and proxies use separate background workers and are collected with `pluto_backend_poll` so rendering stays responsive.
- `Frontend/backend_bridge.h` loads the DLL dynamically. GitHub Actions builds the C UI and Rust DLL together and publishes a lightweight artifact plus a full FFmpeg-bundled artifact.

The FFI accepts and returns UTF-8 JSON strings. Strings returned by the backend must be released with `pluto_backend_free`. See the exported function signatures in `Frontend/backend_bridge.h` and `Backend/backend-ffi/src/lib.rs`.

## Implemented engine paths

- Timeline get/add/move/split/delete, snapping, undo/redo, clip properties, keyframe storage, and keyframe interpolation run synchronously in Rust.
- Media inspection, FFprobe metadata, FFmpeg thumbnails, audio waveforms, and export run on the media worker. A dedicated preview worker owns one persistent FFmpeg raw-frame stream, reuses its decoder process and frame buffer across sequential requests, and reopens with input seeking for timeline changes or backward jumps.
- Preview RGBA frames cross the C ABI through `pluto_backend_copy_preview_frame` and update the existing raylib texture directly; preview frames no longer pass through per-frame PNG files or image decoding.
- Preview and export both build video and audio filters through `backend-ffi/src/filters.rs`, including keyframe-driven scale, rotation, opacity, and position via per-frame FFmpeg expressions.
- When available, the preview worker uses cached editing proxies at every preview size; export continues to read original sources. The shared filters keep effect and transition behavior aligned while proxy resolution controls preview fidelity.
- Playback timing traces are emitted to the Windows debugger once per second with end-to-end, queue, stream setup, decode wait, texture upload, and backend render times.

FFmpeg and FFprobe resolve from `PLUTO_FFMPEG_DIR`, the app's adjacent `ffmpeg/` folder, or `PATH`. `Stella-Windows-x64-lite` contains `Stella.exe`, `pluto_backend.dll`, and icons. `Stella-Windows-x64-full` also contains FFmpeg/FFprobe, their shared runtime DLLs, and notices. The lightweight build can use the `ffmpeg/` folder from an earlier full download by placing it beside `Stella.exe`.

Video effects use the shared FFmpeg filter builder for preview and export. A transition is stored on the outgoing clip at `props.transitionOut` with a type and duration; it is used only when a neighboring video clip follows directly on the same track. The renderer overlaps the incoming clip by that duration and composites it over the outgoing clip.

Timeline scrubbing and left/right frame stepping use the current 30 fps timebase. The persistent stream path is newly integrated and still needs runtime verification from a GitHub build on representative media and machines.
