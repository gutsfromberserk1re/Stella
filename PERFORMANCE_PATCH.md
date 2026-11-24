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

## Follow-up: colour consistency, audio lanes, and inspector layout

- Capture the stream-level YCbCr matrix/range when opening each decoder and use that same colourimetry for every frame from the stream. This avoids different decoder outputs applying inconsistent per-frame fallback tags to otherwise identical stacked clips. Tagged BT.709/BT.601/BT.2020 and full/limited range are preserved; untagged HD/SD uses the conventional BT.709/BT.601 fallback.
- Expand the timeline model and frontend mapping to six audio lanes (A1-A6). Audio rows remain hidden on a fresh project, appear when audio is dragged in or separated, and reveal a continuous stack so using A3 also exposes A1/A2 as available lanes. Audio rows use distinct darker lane styling and an audio-note icon. Empty audio rows fade away when no longer needed.
- Choose the first free audio lane when separating audio from a video clip. If A1 overlaps another audio clip at that time, separation now uses A2, then the next available lane, and reports `NoFreeTrack` if all six are occupied.
- Restore horizontal clipping at the shared inspector content boundary. Effects sections now use full-width grow layouts matching the Colors accordion structure, without an extra manually computed width. Text controls no longer sit inside the redundant outer `TextCol` group.

## Follow-up verification status

- Static source checks and delimiter-balance checks passed for the touched Rust and C headers.
- A regression test was added for separated audio selecting A2 when A1 is occupied, plus tests for A1-A6 serialization and the fresh-project V1-only visibility state.
- Rust tests and the Windows application build were not executable in this container (`cargo`/`rustc` are unavailable and the Clay UI headers are not installed). The Windows Actions build and an actual playback check remain required before calling this runtime-verified.
