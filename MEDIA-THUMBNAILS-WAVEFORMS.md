# Media thumbnails and timeline waveform update

- Media-library video thumbnails are generated as cached 320x180 BMPs for video and still-image media. FFmpeg CLI generation is tried first; when that fails, the in-process decoder writes the same low-resolution bitmap where the source codec is supported, without requiring ffmpeg.exe.
- The thumbnail cache key is bumped to `thumb-v3` so older empty/missing thumbnail cache results are not reused.
- Waveform cache key is bumped to `wave-v7`; FFmpeg writes a white, full-amplitude peak waveform (`1024x128`, transparent background) so the peaks remain visible at timeline scale.
- Video clips with attached audio split the thumbnail and waveform into separate bands. The waveform uses the same dark navy-to-cyan gradient as standalone audio clips, and the thumbnail ends cleanly at the divider.
- Audio fade-in/out regions are drawn above the waveform as a white overlay at approximately 35% opacity so the fade handles remain easy to spot.
- The transform Position/Scale link toggle is larger and its row is inset to align its center with the numeric-value controls rather than the full card. The link glyph itself uses more of its button area.

No additional runtime dependencies were introduced.
