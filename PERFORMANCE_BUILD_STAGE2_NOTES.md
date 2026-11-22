# Stella performance build — Stage 2 (decode scheduling, resolution, and proxy quality)

This patch is intended to be applied **over the Stage 1 performance build**. It preserves Stella's Raylib/Clay interface, timeline design, panels, colors, and editing interactions.

## Preview scheduling

- Moves the persistent FFmpeg decoder pool onto a dedicated decode worker.
- Uses a bounded one-request channel so rapid scrubbing cannot create an unbounded queue of stale decode jobs.
- After returning the requested frame, warms up to two upcoming frames in the decoded-frame cache while the preview worker composites the current frame.
- New seek/playback requests interrupt look-ahead before it can waste time decoding obsolete frames.
- Reports the actual number of warmed frames in the F3 performance overlay on the next response.

## Preview quality tiers

The quality menu now contains the four requested tiers: 240p, 480p, 720p, and 1080p. Source-frame decode caps follow the selected tier:

- 240p output: up to 640 source pixels wide
- 480p output: up to 960 source pixels wide
- 720p output: up to 1440 source pixels wide
- 1080p output: up to 1920 source pixels wide

These caps provide modest oversampling before the GPU compositor scales to the exact selected output size, while avoiding unnecessary 1080p conversion work at the lowest setting.

## Proxy quality

Stella previously generated a 720p H.264 proxy at CRF 30 and selected it for preview even when 1080p preview was requested. The proxy cache key is bumped so old proxies are not reused. New proxies are 1080p maximum, H.264 CRF 18, `veryfast`, two encoder threads, 30-frame GOP, and no B-frames. This gives the preview more source detail and keeps seeking/decoding friendly.

## Timeline and UI

No panel layout, timeline appearance, clip styling, controls, or editing interactions are changed. The quality dropdown removes the unused 360p tier and keeps only 240p/480p/720p/1080p.

## Scope and verification

This is another performance step, not complete Yoru parity or a guarantee of 60 fps on every source/hardware combination. Complex preview cases still retain the existing FFmpeg filter-graph fallback. The proxy FFmpeg scale/encode command passed a synthetic 1080p smoke test in the available environment. Rust and the Windows C frontend were not compiled here because Cargo/Rust and the Windows SDK are unavailable.
