# Nature

## Setup
- Native C/raylib UI connects to the Rust C ABI; no Tauri or WebView.
- Rust handles FFmpeg work off the UI thread; GitHub builds the Windows artifacts.

## Done
- Workflow builds lite and FFmpeg-bundled versions.
- FFmpeg media import, regenerated low-res thumbnail and dark-waveform caches, proxy, preview, export, and shared filters.
- Timeline snapping, linked audio, grouping, drag previews, frame stepping, and keyframe markers.
- Shift+K cuts at the playhead; the scissors tool cuts at the pointer.
- Preview has 24/30/60 fps target controls; library-only low-res thumbnails and cached waveforms.
- Timeline swatches stay consistent across cuts and grouped clips; video audio bands are flush and audio-track clips show large, dark waveforms only.
- Grouped clips move together with their offsets preserved, share effects/colors, and can be ungrouped.
- Color wheels use a brighter hue field and a light, always-visible neutral puck.
- Proxy GOP shortened for faster seeks; new cache key regenerates seek-optimized proxies.
- Transitions are being narrowed to 11 expandable families; join controls appear on hover or show the applied transition.
- Knob values type on double-click; app branding uses Stella.ico.
- Posterize and RGB Halftone FFmpeg graphs corrected; core effects and Slide syntax smoke-checked.
- DPI, icons, value editing, panel animations, and timeline colors refined.

## Next
- Finish shared keyframe interpolation.
- Replace per-frame FFmpeg preview processes with persistent decoding for steady playback.
- Verify thumbnails, waveform alignment, preview playback, and transition-family UI in the GitHub build.
- Verify playback, effects, and preview/export matching through the GitHub build.
