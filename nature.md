# Nature

## Current setup
- Native C/raylib UI talks to a Rust C ABI backend. No Tauri or WebView.
- Rust sends media work to FFmpeg/FFprobe on a background worker.
- Preview and export share the FFmpeg filter builder.

## Progress
- GitHub Actions is configured to package one Stella Windows artifact with the UI, Rust DLL, icon, and FFmpeg runtime.
- Removed demo media. Added filename hover tips and a media zoom cap.
- Added preview menu motion, 30 fps scrub snapping, and left/right frame steps.
- Added older video effect toggles and transitions between adjacent clips.
- Timeline keyframes now show as diamonds on their clips.

## Still needed
- Keyframe values are stored, but shared interpolation across all properties is not finished.
- Preview still launches FFmpeg for individual frames; smooth real-time playback needs a persistent decoder.
- New effect rows currently use defaults; add editable controls.
- Run the GitHub build and check preview/export output.

## Next
- Compile and package in GitHub Actions; confirm the single artifact works on Windows.
- Verify effect and transition output against preview frames.
- Build shared keyframe interpolation and improve preview playback performance.
