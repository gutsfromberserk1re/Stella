# Stella Rust backend

The native raylib interface talks to the Rust `pluto-backend` C ABI DLL. There is no Tauri or WebView in the application.

## Modules

- `timeline-core/` owns clip data, validation, snapping, and timeline mutations.
- `backend-ffi/` exports a narrow C ABI. Cheap timeline commands are synchronous; FFprobe/media jobs use a background worker and are collected with `pluto_backend_poll` so rendering stays responsive.
- `Frontend/backend_bridge.h` loads the DLL dynamically. GitHub Actions builds the C UI and Rust DLL together and publishes one `Stella-Windows-x64` artifact.

The FFI accepts and returns UTF-8 JSON strings. Strings returned by the backend must be released with `pluto_backend_free`. See the exported function signatures in `Frontend/backend_bridge.h` and `Backend/backend-ffi/src/lib.rs`.

## Implemented engine paths

- Timeline get/add/move/split/delete, snapping, undo/redo, clip properties, and keyframe storage run synchronously in Rust.
- Media inspection, FFprobe metadata, FFmpeg thumbnails, audio waveforms, preview frames, and export run on the backend worker so the C render loop stays responsive.
- Preview and export both build video and audio filters through `backend-ffi/src/filters.rs`.
- The UI loads generated media thumbnails and waveforms and displays the current rendered preview frame.

FFmpeg and FFprobe resolve from `PLUTO_FFMPEG_DIR`, the app's adjacent `ffmpeg/` folder, or `PATH`. The Windows artifact contains `Stella.exe`, `pluto_backend.dll`, icons, and FFmpeg/FFprobe with their shared runtime DLLs and notices.

Video effects use the shared FFmpeg filter builder for preview and export. A transition is stored on the outgoing clip at `props.transitionOut` with a type and duration; it is used only when a neighboring video clip follows directly on the same track. The renderer overlaps the incoming clip by that duration and composites it over the outgoing clip.

Timeline scrubbing and left/right frame stepping use the current 30 fps timebase. Preview frames are still rendered as asynchronous FFmpeg still-frame requests, so the UI stays responsive, but this is not a persistent decoder and may not reach real-time playback rates on slower machines.
