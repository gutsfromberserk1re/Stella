# Audio gain, audio pairing, and transition fixes

## Audio gain
- Expanded clip volume to 0–1000% (up to 10x gain) in the inspector and typed value field.
- Added a unity-gain detent to the audio knob, with a nonlinear curve for better control below and above 100%.
- Kept preview and export gain limits consistent at 10x.
- Preserved a single clip's boosted gain; automatic master headroom now only engages when multiple clips overlap.

## Audio relink and reattach
- Unlinking an audio/video pair now preserves reciprocal clip IDs so the pair can be relinked later.
- The timeline context menu has a dedicated **Relink audio + video** action for previously linked clips.
- Detached embedded audio has a separate **Reattach original audio** action that restores the video's embedded audio and removes the extracted timeline clip.
- Manual relinking supports external audio files with different source in-points.
- Linked trims preserve each source's independent offset and source duration.

## Transitions
- Transition inspector now selects the true next chronological clip on the same video track, matching backend pairing.
- Transition writes always target the outgoing selected clip, avoiding inspector state writing to a different clip.
- Preview and export share the same effective duration cap: the requested duration, at most 2 seconds, and at most half of each adjacent clip.
- The timeline indicator now spans the actual incoming transition interval and ends at the incoming clip's edit edge; its displayed width uses the effective duration.
- Replaced the old hourglass-style marker with a clear duration-range bar and cut marker.

## Verification notes
- Source-level regression tests cover audio gain above 600%, the 10x ceiling, independent source offsets on linked audio, unlink/relink metadata, detached-audio reattachment, and transition pairing/duration caps.
- A native compile/test run was not possible in this container because Cargo/Rust and the project's Raylib/Clay build dependencies are unavailable.


## Preview cadence and effect accordion follow-up

- Playback now submits one preview request per selected-rate frame index instead of
  using a half-frame time threshold. Requests are timestamped at exact 24/30/60 fps
  frame boundaries, avoiding refresh-tick jitter and duplicate work at 24/30 fps.
- Playback frame acceptance now allows at most one frame of request age, instead of
  the former 200ms minimum that could display several frames behind the playhead.
- Video Effects accordion expansion is now preserved when the selected clip's
  properties are rehydrated after a slider, value-box, mode, reset, or Enabled change.
  The accordion resets only when the selected clip identity changes.
- Display cadence note: 24 fps on a 60 Hz monitor necessarily presents with a 2:3
  refresh cadence (roughly 2 then 3 display refreshes per video frame). That is the
  monitor refresh mismatch, not a dropped-frame scheduler bug; 120/144 Hz displays
  can present 24 fps with an even refresh cadence.
