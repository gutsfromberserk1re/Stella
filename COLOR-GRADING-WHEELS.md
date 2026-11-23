# Stella color-grading wheel rendering update

Integrated the supplied `draw(1).h` wheel artwork into `Frontend/draw.h` while preserving the current project’s media/timeline styling.

- Bakes the conic hue wheel and radial neutral centre into a single cached RGBA texture.
- Uses 4x supersampling for clean wheel edges and a soft inset shadow/rim.
- Uses cached anti-aliased puck/shadow textures; wheel textures are rebuilt only when UI diameter or DPI scale changes.
- Shares the same wheel texture across the three same-sized grading controls.
- Releases the wheel and puck GPU textures in `UnloadUiIcons`.
- Keeps the existing `WheelLogic` / `WheelItem` interaction and live grading updates unchanged.
- Retains the current blue/cyan detached-audio gradient, grid, and taller attached-media waveform rendering.
- Retains the larger link icon sizing from the supplied header.

No new runtime dependencies were added.
