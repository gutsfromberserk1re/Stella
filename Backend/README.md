# Pluto Rust backend

The native raylib interface talks to the Rust `pluto-backend` C ABI DLL. There is no Tauri or WebView in the application.

## Modules

- `timeline-core/` owns clip data, validation, snapping, and timeline mutations.
- `backend-ffi/` exports a narrow C ABI. Cheap timeline commands are synchronous; FFprobe/media jobs use a background worker and are collected with `pluto_backend_poll` so rendering stays responsive.
- `Frontend/backend_bridge.h` loads the DLL dynamically. GitHub Actions builds it and packages it beside `UI.exe`.

The FFI accepts and returns UTF-8 JSON strings. Strings returned by the backend must be released with `pluto_backend_free`. See the exported function signatures in `Frontend/backend_bridge.h` and `Backend/backend-ffi/src/lib.rs`.

## Implemented engine paths

- Timeline get/add/move/split/delete, snapping, undo/redo, clip properties, and keyframe storage run synchronously in Rust.
- Media inspection, FFprobe metadata, FFmpeg thumbnails, audio waveforms, preview frames, and export run on the backend worker so the C render loop stays responsive.
- Preview and export both build video and audio filters through `backend-ffi/src/filters.rs`.
- The UI loads generated media thumbnails and waveforms and displays the current rendered preview frame.

FFmpeg and FFprobe resolve from `PLUTO_FFMPEG_DIR`, the app's adjacent `ffmpeg/` folder, or `PATH`. GitHub Actions builds the native UI and Rust DLL and places both in the UI artifact; the runner does not currently bundle FFmpeg itself.
