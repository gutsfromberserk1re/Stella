# Nature

## Setup
- Stella uses a native C/raylib UI and a Rust C ABI backend. No Tauri or WebView.
- Rust runs FFmpeg tasks off the UI thread; GitHub builds the app.

## Done
- Added lite and FFmpeg-bundled Windows workflow artifacts.
- Added media import, thumbnails, waveforms, FFmpeg preview/export, and effect filters.
- Added timeline snapping, frame stepping, drag previews, track expansion, keyframe markers, grouping, and audio linking.
- Updated DPI-aware text and ICO tool icons; value pills support typing and horizontal scrubbing.
- Refined cool colors, timeline lane bands, toolbar grouping, and icon sizing.

## Next
- Finish shared keyframe interpolation across properties.
- Improve preview playback with a persistent decoder and bounded frame cache.
- Verify both GitHub artifacts and compare preview with exports.
- Use Shift+K to cut selected clips at the playhead; the scissors tool cuts at the pointer.
- Reduced media thumbnails to 160x90 and removed their translucent purple wash.
- Preview resolution and frame-step changes now request refresh without the normal debounce.
- Transition buttons align to the visible join between adjacent clips.
- Added content-addressed thumbnail and waveform caches plus background FFmpeg proxy generation (720p editing copies; originals remain for export).
- Preview requests use cached proxies when available; linked position/scale values update together while dragging.
- Next: verify proxy fallback and linked-value behavior; add a bounded hot-frame/effect memory cache and optimize continuous playback with a persistent decoder.
