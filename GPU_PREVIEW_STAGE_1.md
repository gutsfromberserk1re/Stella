# Stella GPU preview migration — Stage 1

This stage adds the first native wgpu compute renderer module to the Rust backend.

## What is in place

- `wgpu` device and queue initialization, requested lazily on first renderer use.
- A reusable compute pipeline using WGSL (`backend-ffi/src/preview.wgsl`).
- A frame-in/frame-out RGBA processing API in `backend-ffi/src/gpu_preview.rs`.
- GPU adapter/device failures are returned as errors rather than panicking.
- The existing FFmpeg preview path and C ABI remain unchanged in this foundation stage.

## Important limitation

The current preview stream still asks FFmpeg to decode, apply the full filter graph, composite tracks, and emit a final RGBA frame. This stage does **not** claim to have moved those operations to wgpu. The initial WGSL kernel is an identity pass used to establish and validate the GPU frame-processing boundary. It is intentionally not connected to playback yet: routing every frame through a GPU upload and CPU readback before the renderer is ready would add latency without improving the image.

## Next implementation steps

1. Split preview decoding from timeline rendering: FFmpeg should supply source frames, not a fully filtered/composited timeline frame.
2. Upload decoded frames to persistent GPU textures; avoid allocating buffers and copying the entire frame on every tick.
3. Implement per-clip transform/crop/opacity and track compositing in WGSL.
4. Add color grading and effects as ordered shader passes.
5. Reuse the same render graph for export, with full-resolution targets and FFmpeg encoding only the rendered frames.
6. Add reference-frame tests comparing preview and export outputs within a defined color tolerance.

The old FFmpeg filter graph must remain available as a fallback until each GPU operation has a verified replacement.
