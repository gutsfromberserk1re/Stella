# Preview performance patch

## Why this change was made

The supplied F3 capture reports a 720p/30 preview taking about 102.7 ms per frame, with roughly 69.6 ms spent waiting for decode and 10.3 ms in GPU composite/readback. The timeline shown contains the same MP4 on multiple video tracks at different timeline start positions. At a common playhead, those clips therefore request different source timestamps from one media file.

The old decoder pool keyed its open FFmpeg decoder only by file path. It reused one decoder cursor for all those clips, so requests from different source offsets could repeatedly seek that one decoder back and forth. This is a poor fit for multi-track timelines.

## Changes

- Keep one persistent sequential FFmpeg decoder context per timeline clip (clip ID), rather than one context per media path.
- Continue sharing the decoded-frame LRU cache globally by media path, source frame index, and output width. Two clips requesting the exact same source frame can still reuse the same pixel buffer.
- Increase the bounded open-decoder-context budget from 6 to 12 to cover common multi-track timelines without making the pool unbounded.
- Treat output width as a per-frame conversion parameter. Animated scale and preview-quality changes no longer force an FFmpeg input/decoder reopen for that clip.
- Aggregate hardware-transfer and pixel-conversion timings across all source frames in the current preview batch. The F3 breakdown previously reported only the last converted frame, which understated multi-layer conversion work.
- Keep preview output dimensions, oversampling, pixel conversion, filtering, and shader quality unchanged. No quality-reducing resolution or frame-skip workaround was introduced.

## Verification status

- Source changes and archive structure were inspected.
- `git diff --check` could not be used because the extracted archive is not a Git working tree; whitespace and patch locations were reviewed directly.
- A Rust build/test could not be run in this container because `rustc` and `cargo` are not installed.
- This patch therefore needs the repository's Windows GitHub Actions build and a runtime A/B measurement on the same project. Compare F3 decode wait, conversion total, composite/readback, frame interval median/p95, and displayed FPS before/after.

## Remaining performance work

The WGPU compositor still performs a blocking GPU readback so the existing Raylib/OpenGL UI can upload the result as a CPU-backed texture. That path is measured separately and remains a later zero-copy presentation milestone. This patch addresses the avoidable decoder seek contention first; it does not claim the whole editor has reached real-time playback yet.
