# Stella performance patch

## Changes

- Removed 4x MSAA from the raylib window. Stella's editor is predominantly 2D; multisampling the whole window adds GPU work without materially improving the UI at normal display scale.
- Timeline lanes now skip creating Clay clip nodes for clips fully outside the visible horizontal viewport. The timeline still keeps its clip data and magnetic layout; only invisible UI nodes are culled.
- Increased the bounded FFmpeg preview read-ahead queue from 6 to 8 frames to absorb short scheduling/decode spikes. At 720p, the maximum queued RGBA payload is about 29 MiB; memory remains bounded.

## Important limitation

Preview rendering is still a single in-flight request pipeline. The decoder reads ahead, but each displayed frame is requested, rendered, published, copied over the C ABI, and uploaded to the raylib texture before the next request is submitted. These changes reduce UI and short-stall overhead; they do not guarantee real-time playback when the FFmpeg filter graph cannot sustain the selected frame rate. A continuously presented timestamped frame stream and GPU-resident effects/compositing are the larger architectural improvements in the roadmap.

## Verification

- Source-level edits only. This environment has no Cargo/Rust toolchain or Windows build toolchain, so the C frontend and Rust backend could not be compiled or runtime-profiled here.
- Test on Windows with 720p/1080p media at 24/30/60 preview FPS. Compare `[Stella preview]` debugger timings: `decodeWaitMs`, `streamSetupMs`, `uploadMs` (reported as upload in the frontend trace), and `end-to-end`.
- Test long timelines with many clips while scrolling and zooming. Off-screen clips should remain editable once brought into view, and visible clip/transition hit-testing should be unchanged.
