# Stella timeline, inspector and export fixes

## Timeline tracks
- V1, V2, V3 and V4 remain visible as a stable stack, including empty lanes; unused lanes no longer collapse.

## Timeline keyframes
- Keyframe timestamps are absolute timeline times. Clip moves, magnetic gap-closing, ripple shifts, left trims and linked video/audio moves now shift the keyframes by the same actual start-time delta.
- Timeline keyframe diamonds and their hit targets are 2 px larger.

## Clip audio display
- Video thumbnails end at a clear divider above the waveform strip.
- Waveforms are regenerated using a full-amplitude white peak display on the same navy-to-cyan gradient used by standalone audio clips.
- Fade-in and fade-out regions use a white overlay at approximately 35% opacity, drawn over the waveform.

## Effects and filters
- Expanded Transitions content is width-constrained and horizontally clipped within the inspector.
- Transitions, Filters, Video Effects and individual effect rows use the same rounded 48 px card treatment as the Colors accordions.
- The filter browser includes 12 presets grouped into All, Color, Film and Mono. Teal & Orange, Sepia, Bleach Bypass and Pastel have matching native-preview and FFmpeg-export settings.

## Export path safety
- Export now checks that the destination is not the same resolved path as any source input and reports a clear error before starting FFmpeg.
- The default export filename is `stella-exported.mp4`, avoiding the previous common collision with a source named `stella-export.mp4`.

## Validation
- FFmpeg syntax checks passed for the waveform generation options and all four new look filter graphs.
- Rust unit tests and the native Windows frontend build were not run in this environment because the Rust toolchain and frontend SDK headers are not installed. Re-run the project's Windows CI workflow to complete validation.

## Timeline tracks, trimming and control reset
- Video tracks V1-V4 and audio tracks A1-A2 remain available when empty.
- Dragging a visible clip edge trims the source, and dragging back restores any remaining source footage. Linked audio and keyframes stay attached during the trim.
- Selected clips receive a subtle white outline.
- Double-clicking an audio knob resets it to its neutral/default value; double-clicking a grading puck recentres that wheel.
- Rounded UI curves use denser geometry and the knob texture is supersampled for cleaner high-DPI rendering.
