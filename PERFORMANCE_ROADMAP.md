# Stella playback performance roadmap

## Stage 1 — preview delivery (source patch)

- Persistent FFmpeg process with a dedicated decoder thread.
- Bounded six-frame read-ahead queue with reusable RGBA buffers.
- Wall-clock playback scheduling and obsolete-frame skipping.
- FFmpeg auto thread sizing and opportunistic hardware decode for video inputs.
- Direct RGBA upload to the existing raylib preview texture.

## What Stage 1 does not solve

The current filter graph still downloads/produces CPU RGBA frames and runs effects/compositing through FFmpeg filters. Hardware decode can reduce codec-decoding cost, but it does not turn the current effects pipeline into a GPU renderer. A six-frame queue also cannot compensate for a filter graph that takes longer than the selected frame interval.

## Stage 2 — GPU-resident preview compositor

1. Keep decoded video frames in GPU surfaces/textures where supported; avoid downloading every source frame to CPU memory.
2. Build a render graph for clip transforms, opacity, blend modes, masks, color correction, blur, and transitions.
3. Cache effect graph state and intermediate textures; invalidate only when the playhead, clip properties, or source frame changes.
4. Use timestamped video/audio clocks and a presentation scheduler; do not let UI frame rate control media time.
5. Keep a CPU/FFmpeg fallback for unsupported codecs, effects, and export.
6. Add preview-vs-export reference tests for every effect and transition before expanding the GPU path.

## Stage 3 — adaptive playback and caching

- Prewarm proxies and keyframes for clips near the playhead.
- Adapt preview resolution/quality when decode or render falls behind, then restore quality when headroom returns.
- Maintain a bounded decoded-frame cache and a separate rendered-frame cache.
- Prioritize interactive playback over background proxy generation.

## Verification

- Test 720p and 1080p H.264/H.265 sources at 24, 30, and 60 FPS.
- Test two and three simultaneous video layers, transitions, and representative effects.
- Record dropped frames, decoder wait, render time, texture upload time, queue occupancy, and audio/video drift.
- Check actual Windows hardware decode activity; an FFmpeg `-hwaccel auto` option alone does not prove that the driver selected hardware.
