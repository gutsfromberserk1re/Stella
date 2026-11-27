# Speed Control (initial constant-speed pass)

- Added a Speed control directly below Opacity in the Transform inspector, before the Crop Tools / Distort navigation.
- Added editable speed value and 0.5x, 1x, 1.5x, and 2x quick presets. Values are clamped to 0.25x–4x.
- Stored speed in `props.transform.speed` (default 1.0 for existing projects).
- Native preview and export video frame selection now map elapsed timeline time to source time using the same speed multiplier, including transition source-time calculations.

## Current scope / known limitations

This is the first constant-speed implementation. It changes video source playback rate, but does not yet resize the timeline clip duration or ripple following clips. Reverse, pitch-preserving audio retiming, audio speed synchronization, and speed ramps are not implemented in this pass. Those need dedicated timeline/audio-engine work rather than being simulated in the inspector.
