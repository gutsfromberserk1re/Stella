# Media thumbnails and timeline waveform update

- Media-library video thumbnails are generated as cached 320x180 BMPs for video and still-image media. FFmpeg CLI generation is tried first; when that fails, the in-process decoder writes the same low-resolution bitmap where the source codec is supported, without requiring ffmpeg.exe.
- The thumbnail cache key is bumped to `thumb-v3` so older empty/missing thumbnail cache results are not reused.
- Waveform cache key is bumped to `wave-v6` and FFmpeg now draws a white waveform. The frontend composites it at 80% opacity.
- Video clips with attached audio get a 16 px waveform band at the bottom; clip labels are top-aligned to keep the waveform legible.
- Standalone audio clips use a dark navy-to-cyan gradient and a subtle low-contrast grid inspired by the supplied reference.
- The transform Position/Scale link toggle is larger and its row is inset to align its center with the numeric-value controls rather than the full card. The link glyph itself uses more of its button area.

No additional runtime dependencies were introduced.
