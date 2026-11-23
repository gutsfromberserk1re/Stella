# Stella — Effects Panel and Text Integration

## Effects panel scrolling

Expanded transition, filter, and effect cards now use their natural content height. The outer inspector body remains the single clipped/scrollable viewport, avoiding the nested fixed-height wrappers that caused the video-effects rows to be clipped when Transitions was expanded.

## Live colour and effect editing

- Colour grading wheels send lightweight live property updates while dragging and commit one undo transaction when the mouse is released.
- Colour sliders send live values while dragging; numeric entry remains a single committed edit.
- Video-effect sliders use the same live-update path.
- Slider sent-values are rehydrated when the selected clip changes, so a previous clip's cached UI value is never pushed onto the newly selected clip.

## Text tool

The Text tab now creates real timeline title layers rather than decorative UI controls.

- `T` or the timeline Text tool opens the Text tab and activates placement mode.
- Click in the content field to type. Enter inserts a line break; Backspace deletes a code point.
- Choose font size, colour, alignment, bold, outline, shadow, and a background plate.
- Activate placement and click inside the preview to position the title.
- Titles are transparent 1920x1080 PNG overlays stored under `%LOCALAPPDATA%\\Stella\\Titles` on Windows, inserted as image clips on the upper video track. They can be moved and trimmed like other clips.
- Selecting a title hydrates its settings into the Text tab. **Update selected title** replaces its image and metadata; **Create another title** adds a separate overlay.
- The backend accepts updates only for Stella title clips and records the change in undo history.

The PNG is generated only when a title is applied or placed, not on every UI frame, so text rendering does not add a continuous preview cost.

## Validation performed

- Checked all edited C/header delimiter pairs.
- Compiled and executed a C99 smoke test of the title-metadata JSON formatter; parsed its output including quotes, backslashes, and newlines as valid JSON.
- Checked the Rust title-update branch's balanced delimiters and its matching frontend command name.
- Confirmed the new text controls use Clay's dynamic-string ID macro (`CLAY_SID`).

A full Windows build and Rust test run still need to be performed by the project's GitHub Actions workflow, because this environment does not include the project's Rust/FFmpeg/LLVM/Clay/Raylib Windows build toolchain.
