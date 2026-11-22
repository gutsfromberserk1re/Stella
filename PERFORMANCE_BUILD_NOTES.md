# Stella performance build — first playback integration

This patch preserves Stella's raylib/Clay interface and changes only the preview backend and timeline layout work.

## Preview changes

- Activates Stella's existing in-process `ffmpeg-next` decoder pool for ordinary video clips.
- Reuses decoder contexts across preview requests instead of starting a new FFmpeg CLI filter-graph process for every normal frame.
- Reuses decoded frames through the existing bounded LRU frame cache.
- Sends decoded source frames through Stella's existing persistent wgpu layer renderer for transforms, grading, effects and track compositing.
- Keeps the existing FFmpeg filter-graph preview as a correctness fallback for active transitions, still images, keyframed clips, and unsupported media/decoder/GPU cases.
- Limits the open decoder pool to six sources.

## Timeline changes

- Replaces per-lane insertion sorting with `qsort` (`O(n log n)` instead of `O(n^2)`).
- Avoids creating Clay clip elements for clips beyond the visible right edge of the timeline lane. Off-screen clips remain in the timeline model and become visible when panned into view.
- Does not change the timeline's appearance, clip geometry, labels, colors, or editing interactions.

## Important scope note

This is the first integration step, not complete Yoru playback parity yet. Stella still does not have Yoru's dedicated bounded decode-ahead worker queue or audio-master playback clock. Complex preview cases deliberately remain on the established FFmpeg graph until their native GPU behavior is verified. The current GPU renderer also performs a readback because Stella's existing Raylib preview surface consumes CPU RGBA pixels.

The source environment used to prepare this patch did not have Cargo/Rust installed, so a Rust build and runtime playback test could not be run here. Please run the repository's Windows GitHub Actions workflow before treating this as a verified build.
