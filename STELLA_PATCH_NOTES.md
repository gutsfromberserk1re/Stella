# Stella playback and inspector patch

## Included changes

### Preview playback
- Keeps FFmpeg in a persistent preview stream.
- Decodes ahead on a dedicated thread into a bounded six-frame queue.
- Recycles RGBA frame buffers instead of allocating a new full-size frame for every decode.
- Publishes newly decoded frames correctly, including the first frame.
- Advances the playhead from a monotonic wall clock and skips obsolete requests instead of building a late-frame backlog.
- Allows FFmpeg to select decoder/filter thread counts automatically and requests `-hwaccel auto` for video inputs.

### Effects inspector overflow
- Transition details now use their natural layout height and do not create a second clipped viewport inside the Effects inspector.
- The inspector remains the single vertical clipping/scrolling boundary. This prevents the transition section from interfering with clipping of the effect cards below it.
- Stella's custom panel scroll offsets are the source of truth. Wheel deltas are no longer sent into Clay's separate scroll accumulator at the same time.
- The renderer's scissor stack is more defensive and preserves parent clipping if unusually deep nesting occurs.

## Important performance boundary

This is a playback-delivery and decode-throughput improvement, not yet a complete Final Cut Pro-class GPU rendering engine. `-hwaccel auto` can accelerate supported video decoding when the installed Windows driver and bundled FFmpeg build expose a compatible device. The FFmpeg filter graph still performs effects/compositing on the CPU and produces CPU-side RGBA frames for raylib. A GPU-resident compositor/effect graph is the next major architecture phase.

## Build and verify

Run from the repository root on Windows with Rust, MSYS2 UCRT64, and the workflow dependencies available:

```powershell
cargo test --manifest-path Backend/Cargo.toml
cargo build --release --manifest-path Backend/Cargo.toml -p pluto-backend-ffi
```

Then run the existing `.github/workflows/thing.yml` workflow to build the native C/raylib UI and package the backend DLL.

### Playback verification
- Test 720p/1080p H.264 and H.265 at 24, 30, and 60 FPS.
- Test multiple video layers, transitions, and several enabled effects.
- Check the `[Stella preview]` debugger trace: `decodeWaitMs`, total backend time, and upload time.
- Check Task Manager's GPU Video Decode graph; the presence of `-hwaccel auto` does not prove hardware decoding was selected.

### Inspector regression verification
- Select Effects, open Transitions, then scroll to the bottom of Video Effects. Every effect row must stay inside the right inspector boundary.
- Repeat with Transitions closed, Filters expanded, and one Video Effect expanded.
- Switch between Transform, Colors, Effects, and Text after scrolling to verify the scroll position resets and no stale offset remains.

## Verification status

The source patch was applied to the supplied Stella archive. A native build and runtime test could not be performed in this environment because the Rust toolchain and Windows UCRT64/raylib build dependencies are not installed here. Treat the included source as patched but not yet build-verified.
