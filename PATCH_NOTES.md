# Stella source patch notes

This source update covers the timeline, preview, color, effects, text, subtitle, font, and media-relink issues reported with the supplied project.

## Included

- Serialize preview rendering to one in-flight request and reject stale/out-of-order frames before they replace the visible preview.
- Split linked audio/video clips together, preserve reciprocal links and source offsets, and keep linked trims aligned.
- Add context-menu actions to link an independently placed audio/video pair and relink a timeline clip through a dedicated Windows file picker. The replacement is inspected and checked against the target track type before relinking.
- Add a playhead scissors tool (K); Shift+K splits video tracks, Ctrl+K splits audio tracks, and Ctrl+Shift+K splits both. The pointer scissors tool ignores nearby double-click repeats.
- Keep Text tool clicks usable by creating a default text box for a click-sized gesture.
- Keep the Subtitles accordion and rounded plus button in the Text tab as a coming-soon entry point. No Whisper executable, model, transcription service, or subtitle-generation setup is required or invoked at this stage.
- Preserve subtitle timeline support for a later implementation: subtitle clips bypass magnetic placement/ripple, can overlap on their synthetic V6 backing track, and do not affect regular video clips during move, trim, or delete operations.
- Show a separate `S` lane only while subtitle clips exist. The expanded Subtitles card lists existing cue text and time ranges, and clicking a cue selects it and moves the playhead to its start. Subtitle clips are composited above regular video in both preview and export.
- Enumerate installed Windows TrueType/OpenType fonts from the system and per-user font registries. The font picker is a clipped, scrollable popup; selected font names and file paths are saved with title clips.
- Separate exposure from brightness and use dedicated contrast/shadow/highlight/black/white grading paths. Add the Retro filter preset and the eight-preset layout.
- Keep Blur and Grain in Filters, with their expanded controls, and make the effect-card header an accordion control while the body switch controls enablement. Vignette remains in Colors → Detail, not as an Effects card.

## Verification

The source files and ZIP archive were checked for consistency and archive integrity. This environment does not contain Cargo/Rust, Raylib headers, or Clay headers, so the Rust tests and native application could not be compiled or run here. A local Windows build and playback/export pass are still required.

## CI compile fix — Clay horizontal layout
- Replaced the undefined `ROW` layout shorthand in the Subtitles empty-state and cue rows with Clay's explicit `CLAY_LEFT_TO_RIGHT` layout direction.
- Checked the frontend sources for any remaining `ROW` references; none remain.
