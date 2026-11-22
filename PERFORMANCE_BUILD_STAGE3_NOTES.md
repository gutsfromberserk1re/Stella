# Stella performance build, Stage 3: Yoru's preview organs

Stage 3 ports the parts of Yoru's preview engine that make it smooth into Stella's Rust backend
and C frontend. Stella's raylib/Clay UI, timeline appearance, panels and editing behaviour are unchanged.

## What was wrong (read from the code, and from the F3 overlay at 480p/60)

The overlay showed `decode wait 24.6 ms` out of `backend 34.7 ms`, and `interval 49.4 ms`.

1. **Conversion was the bottleneck.** Every decoded frame was converted to RGBA at full source size
   (about 2.07 M colour conversions at 1080p) and then box-filtered with f64 math (about 8.3 M
   multiplies) down to the preview size. Yoru gets its scaling from swscale; Stella's decode-only
   static FFmpeg has no swscale, so the port is a custom fused kernel (below).
2. **Per-frame timeline round trip.** `submit` cloned the whole timeline, serialized it to JSON on the
   UI thread, and the worker parsed it again, for every preview frame.
3. **Debounce stall.** The UI waited 16 ms before sending the next request while playing or scrubbing.
   Because requests are polled once per UI loop iteration, a 40 ms request showed up as a ~50 ms
   interval (3 loop iterations), which matches the screenshot.
4. **Look-ahead during scrubs.** Decode-ahead also ran while scrubbing, delaying the next seek.
5. **Frame grid mismatch.** Arrow-key stepping, the prev/next-frame buttons, scrub snapping and the
   timecode all used a hard-coded 30 fps grid even when the preview was set to 24 or 60.

## What was ported / changed

| Yoru organ | Stella change |
|---|---|
| Convert only what is displayed, scale during conversion | `decoder.rs`: `scale_yuv_to_rgba` / `scale_packed_to_rgba` convert and area-average in one pass, one colour conversion per *output* pixel, integer math only. Covers 8-bit and 9-16-bit planar YUV, NV12/NV21, P010, packed RGB(A). |
| Hardware decode with software fallback | `decoder.rs`: CUDA -> D3D11VA -> DXVA2 on Windows, used for sources 1920 px wide and up (`STELLA_HWDEC=1/0` forces on/off). Surfaces are copied to NV12/P010 and fed to the same converter. Any HW failure reopens that file in software. Thumbnails never use HW. |
| Seek fallback | A container that rejects a timestamp seek is reopened and scanned forward instead of leaving the preview stuck. A damaged software packet is skipped instead of aborting the frame. |
| Reverse-step history (`Player::past`) | During an arrow-key step-seek, the 8 frames before the target are converted and cached, so repeated reverse steps are instant. Plain scrubbing keeps the cheap discard-before-convert path. |
| Decode ahead only while playing, depth 3 (`VIDEO_QUEUE`) | Look-ahead runs only when the transport is playing and counts *source* frames. |
| (Stella-specific) | Preview requests reuse an `Arc<Timeline>` snapshot that is rebuilt only when the timeline changes. |
| (Stella-specific) | Frontend: no debounce while playing/scrubbing/stepping; every 30 fps constant now follows the preview fps; F3 shows `decode: HARDWARE/SOFTWARE`. |

## Measured here

The fused kernel's maths was checked in Python against Stella's previous pipeline on a 1080p test frame:
mean difference 0.036/255, 0.26% of pixels differ by more than 2 (saturated colour edges only). Against
ffmpeg's own area scaler, mean difference was 0.96/255 (spikes at hard colour edges, where swscale
interpolates chroma and this kernel samples the nearest chroma value, as the old code did).
Colour conversions per 1080p -> 960x540 frame: 2,073,600 -> 518,400, and no floating-point work.
Unit tests for the new pure functions were added to `decoder.rs`.

## NOT verified

No Rust toolchain, raylib, or Windows SDK was available where this was prepared, so **nothing has been
compiled or run**, and no frame-rate numbers were measured. Run the GitHub Actions workflow (it runs
`cargo test` first) before trusting any of it. Compile errors in the HW-decode block are the most
likely thing to need a touch-up; set `STELLA_HWDEC=0` to rule hardware decode in or out at runtime.

## Still not like Yoru

- No audio-master clock: Stella has no audio playback, so playback timing is still wall-clock driven.
- Requests are still single-flight (one in flight, polled once per UI loop). If a frame still costs
  more than one vsync after these fixes, the next step is allowing two in flight with a small
  frame-slot ring.
- Preview still reads back from the GPU into a raylib texture (not zero-copy).
- Long-GOP originals still cost a keyframe-to-target decode on backward scrubs; generate a proxy.
