# Stella source patch notes

This source update covers the timeline, preview, color, effects, text, subtitle, font, and media-relink issues reported with the supplied project.

## Included

- Serialize preview rendering to one in-flight request and reject stale/out-of-order frames before they replace the visible preview.
- Split linked audio/video clips together, preserve reciprocal links and source offsets, and keep linked trims aligned.
- Add context-menu actions to link an independently placed audio/video pair and relink a timeline clip through a dedicated Windows file picker. The replacement is inspected and checked against the target track type before relinking.
- Add a playhead scissors tool (K); Shift+K splits video tracks, Ctrl+K splits audio tracks, and Ctrl+Shift+K splits both. The pointer scissors tool ignores nearby double-click repeats.
- Keep Text tool clicks usable by creating a default text box for a click-sized gesture.
- Add a Subtitles accordion with a real generation action. It extracts the selected video's trimmed audio, runs `whisper-cli`, imports timed SRT cues as text clips, and replaces only that source's previous generated captions in one undoable operation.
- Preserve SRT cue timestamps exactly: subtitle clips bypass magnetic placement/ripple, can overlap on their synthetic V6 backing track, and do not affect regular video clips during move, trim, or delete operations.
- Show a separate `S` lane only while subtitle cues exist. Captions are presented in that lane, the expanded Subtitles card lists cue text and time ranges, and clicking a cue selects it and moves the playhead to its start. Captions are composited above regular video in both preview and export.
- Store the Whisper executable and model paths in `stella-subtitles.ini` beside the application. The first generation asks for `whisper-cli.exe` and a Whisper `.bin` model. FFmpeg must also be available through the application's bundled media tools or configured FFmpeg directory.
- Enumerate installed Windows TrueType/OpenType fonts from the system and per-user font registries. The font picker is a clipped, scrollable popup; selected font names and file paths are saved with title clips. Selecting an existing subtitle keeps the generator targeted at its original source video.
- Separate exposure from brightness and use dedicated contrast/shadow/highlight/black/white grading paths. Add the Retro filter preset and the eight-preset layout.
- Keep Blur and Grain in Filters, with their expanded controls, and make the effect-card header an accordion control while the body switch controls enablement. Vignette remains in Colors → Detail, not as an Effects card.

## Verification

The source files and ZIP archive were checked for consistency and archive integrity. This environment does not contain Cargo/Rust, Raylib headers, or Clay headers, so the Rust tests and native application could not be compiled or run here. A local Windows build and playback/export pass are still required.
