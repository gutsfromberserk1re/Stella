# Nature

## Setup
- Native C/raylib UI connects to the Rust C ABI; no Tauri or WebView.
- Rust handles FFmpeg work off the UI thread; GitHub builds the Windows artifacts.

## Done
- Workflow builds lite and FFmpeg-bundled versions.
- FFmpeg media import, proxy/waveform caches, preview, export, and shared filters.
- Timeline snapping, linked audio, grouping, drag previews, frame stepping, and keyframe markers.
- Shift+K cuts at the playhead; the scissors tool cuts at the pointer.
- Playback accepts late frames and advances on a 30 fps clock.
- Media thumbnails removed; stable non-blue swatches and larger blue waveforms added.
- DPI, icons, value editing, panel animations, and timeline colors refined.

## Next
- Finish shared keyframe interpolation.
- Replace per-frame FFmpeg preview processes with persistent decoding.
- Verify playback and preview/export matching through the GitHub build.
