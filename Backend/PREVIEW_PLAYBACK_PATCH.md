# Stella preview playback patch

## What changed

- FFmpeg stdout is now read continuously on a dedicated `stella-preview-decode` thread.
- Decoded frames are placed in a bounded eight-frame queue. This gives the renderer a small read-ahead buffer without letting memory usage grow with timeline duration.
- Each queued frame carries its sequential frame index. The preview worker consumes up to the requested frame and recycles obsolete frames rather than uploading them late.
- Raw RGBA buffers are recycled between the decoder and consumer to avoid allocating a full frame buffer for every frame.
- The decoder thread is stopped and joined when the stream is replaced or the preview engine is dropped; the frame receiver is disconnected first so a producer blocked on a full queue can exit.
- The first-frame publication path is guarded: an empty or differently sized shared frame slot is populated even if the caller says the last frame can be reused.
- Playback time now advances from `GetTime()` wall-clock deltas. The selected preview FPS controls request cadence, while missed preview intervals collapse to the latest playhead position instead of building a backlog.
- A Rust regression test covers publishing the first frame when the reuse hint is set but the shared slot is empty.

## Build and test

Run the Rust tests from the repository root:

```powershell
cargo test --manifest-path Backend/Cargo.toml
```

Build the Rust backend:

```powershell
cargo build --release --manifest-path Backend/Cargo.toml -p pluto-backend-ffi
```

Build the Windows UI using the repository's `.github/workflows/thing.yml` workflow, then test playback with:

1. A 720p H.264 clip at 24, 30, and 60 FPS preview settings.
2. A 1080p source with its generated editing proxy available.
3. A 1080p source before its proxy finishes generating.
4. A timeline with multiple consecutive clips and a transition.
5. Pause/resume, frame stepping, backward seek, and forward seek.
6. Changing preview resolution while playback is stopped and while it is running.
7. Open Transitions, scroll to the bottom of Effects, and verify every effect card remains clipped inside the inspector; repeat with Transitions closed and with Filters expanded.
8. On Windows, inspect the debugger trace and Task Manager GPU Video Decode graph to confirm whether hardware decode was actually selected. Compare with `-hwaccel none` if the driver path behaves poorly.

The existing `[Stella preview]` debugger trace reports queue delay, stream setup, frame wait/decode, upload, and total backend time. During normal playback, `decodeWaitMs` should generally fall once the queue has filled. If it remains high, the decoder/effects are still unable to sustain the selected frame rate; if decode wait is low but upload time is high, investigate the raylib texture upload path. Hardware acceleration is opportunistic: `-hwaccel auto` only helps when the bundled FFmpeg build, codec, and installed driver expose a usable decoder. The current FFmpeg filter graph still processes effects/compositing on the CPU; this patch is not a GPU effects engine.

## Verification status

The source changes were applied to the supplied project archive. They were not compiled or run in this environment because `cargo`/`rustc` and the Windows UCRT64 toolchain are unavailable here. The Windows workflow must pass before treating this patch as runtime-verified.
