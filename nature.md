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
