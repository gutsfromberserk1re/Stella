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


## Follow-up: effect parity and CI compile repair

- Fixed the colorimetry compile regression: stream-level color space/range live on `VideoDecoder`; they are not fields on `FrameKey`. The frame cache key remains path + frame index + output width, because colorimetry is a stable property of the source stream.
- Replaced the rustdoc comment above `thread_local!` with a normal source comment to remove the unused-doc-comment warning.
- Removed the old `Glow` control. It was not a true highlight bloom; it blurred the whole frame and applied a tone shift, and its export path was not perceptually equivalent to the preview. Existing projects that contain `props.effects.glow` now ignore that legacy property instead of applying a misleading look.
- Kept only effects with an implementation on both paths: Chromatic Aberration, Pixelate, Blur, Vignette, Sharpen, Scanlines, Camera Shake, Film Burn, Halftone, Posterize, Light Leak, and Mirror. The renderer and FFmpeg export paths share the same parameter ranges and effect equations where applicable.
- Added a preview-uniform regression test that verifies the retained effect parameters are actually mapped into renderer uniforms. Export-graph tests cover the retained filter operations and verify removed legacy effects stay disabled.
- FFmpeg smoke-tested the combined export filter chain (Gaussian blur, pixelation, chromatic shift, sharpen, vignette, posterize, halftone, scanlines, film burn, and light leak); the generated one-frame chain ran successfully.
- Moved the export `detail` property lookup before its first use in the sharpen pass.

## Verification status (follow-up)

- Corrected the exact missing-field and misplaced-field issues from the reported `cargo test` failure.
- Static checks confirm `FrameKey` has only its intended three key fields and `VideoDecoder` owns the two stream color fields.
- Cargo is not installed in this execution environment, so the Rust tests still need to be rerun in Windows GitHub Actions. No successful compile is being claimed here.


## Preview pipeline update (November 2026)

The request/render path is no longer a one-frame UI ping-pong:

- The frontend keeps a bounded three-request window. Each outstanding request records its own playhead, resolution, FPS, start time, and title-image path, so out-of-order worker completions can be matched to the correct UI request and stale frames are rejected.
- Two preview render workers can process independent requests. The per-clip FFmpeg decoder coordinator is shared between them: each timeline clip gets one persistent decoder thread and a bounded request queue, while independent visible clips decode concurrently. Decoder prefetch is interrupted as soon as a newer request for that clip arrives.
- Preview workers share a revisioned `Arc<Timeline>` snapshot. A timeline revision is bumped after successful timeline mutations, replacing per-preview full-timeline structural equality checks and repeated timeline JSON conversion on the UI thread.
- Every clip retains its own sequential FFmpeg decoder, while the decoded-frame LRU is shared across clip workers by media path, source frame, and output width. Its total preview budget is capped at 128 MiB rather than multiplying a cache per clip.
- The preview compositor now writes into reusable output vectors. Pixel-row conversion uses a persistent Rayon pool rather than creating/joining OS threads for every decoded frame.
- The UI can take ownership of the completed Rust pixel buffer and return the allocation to the Rust buffer pool after presentation. This removes the extra full-frame copy into a second C buffer. On Windows, same-size texture updates use the PBO staging path described below; unsupported drivers and non-Windows builds retain Raylib's regular upload path. The older copy API remains available as a compatibility fallback.
- During playback, the active audio stream is used as the playhead master when available; video-only playback retains the wall-clock fallback.
- Frame-cache width changes force a seek at the requested source timestamp, avoiding the one-frame-ahead result when animated scale changes the decoder output width.

## Preview pipeline follow-up: asynchronous GPU/audio presentation

The next bottleneck pass addresses the blocking presentation work left by the first pipeline update:

- WGPU preview readback uses three per-resolution staging buffers. Preview workers submit the composite and return a receiver immediately; a dedicated `Device::poll(Maintain::Poll)` thread drives `map_async`, and a non-blocking completion worker polls every outstanding receiver and publishes whichever frame is ready first under its original request ID. One slow map no longer head-of-line-blocks later completed frames. The interactive preview path no longer calls `Maintain::Wait`. The synchronous wrapper remains for export callers.
- F3 reports GPU submission time separately from asynchronous readback-completion time, so submission and map/readback are no longer folded into one opaque composite number.
- On Windows, Raylib/OpenGL presentation uses a three-slot pixel-unpack-buffer ring with non-blocking GL sync-fence polling. A busy ring falls back to Raylib's normal `UpdateTexture` rather than waiting. This avoids one additional client-memory upload stall on the common same-size preview path. It is asynchronous staging, not zero-copy interop.
- Windows audio stream creation runs on a dedicated loader thread. A separate short-interval worker pumps `UpdateMusicStream`; the UI uses a try-lock for play/pause/seek/volume updates and falls back to its wall clock if the pump owns the stream lock for that tick. Load results carry clip and path identities so stale completions are discarded safely. Non-Windows and worker-startup-failure builds retain the synchronous fallback.
- Hardware decode's automatic threshold now uses coded pixel area (defaulting on at 1280x720-equivalent area) rather than a width-only 1920px check. `STELLA_HWDEC=0|1` still forces software/hardware for driver-specific A/B tests; GPU-to-CPU frame transfer can still make hardware decode slower on some machines.
- The compositor can skip uploading/drawing lower layers when a higher layer is proven to be fully opaque and fully cover the output canvas. This is conservative: it requires cached opaque-alpha classification, full opacity, no crop, no corner distortion, no transition, and geometry that covers every output pixel. Source decoding still happens before this proof, so this pass saves GPU texture uploads and fragment work, not decoder work.

### Remaining limitation: true WGPU/OpenGL zero-copy

- WGPU and Raylib currently own separate graphics backends/contexts. The project does not yet have a supported shared-texture import/export path between the selected WGPU adapter and Raylib's OpenGL context. The PBO ring is an asynchronous CPU-to-GL staging path; it does not remove the WGPU readback or CPU-backed pixel buffer. A real zero-copy path requires selecting and implementing a compatible cross-API interop route (and a fallback for adapters/drivers that do not support it), not merely passing a texture handle between APIs.

### Verification status

- Source-level changes and archive integrity are checked for this patch.
- Cargo/Rust and the Windows Raylib/Clay build toolchain are not installed in this container, so Rust tests and the Windows application build cannot be run here. The Windows GitHub Actions build and F3 playback comparison remain necessary before treating any throughput improvement as measured.
- Benchmark at the same resolution, project, playhead, and hardware-decode override. Compare `decodeWaitMs`, `compositeMs`, the new `readbackMs`, upload timing, frame-interval median/p95, and displayed FPS. Do not treat source-level changes as a measured FPS claim.
