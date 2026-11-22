# Stella timeline UI polish

This update refines the native timeline UI without adding upscalers, AI image processing, or new runtime dependencies.

## Changes

- Removed the circular dark backing from keyframe markers. Markers are slightly larger, outlined diamonds with a larger pointer hit target.
- Increased shared UI text raster sizes modestly (about 1.75 logical pixels over the previous setting), including timeline labels and ruler text.
- Kept the existing 4x MSAA window hint and exact-size cached font rasterization. No DLSS-style upscaling or additional graphics runtime is introduced.
- Moved Undo and Redo into the timeline toolbar immediately after Delete. Both actions use the existing Rust timeline history commands and request a fresh preview after success.
- Collapsed unused video lanes while always retaining V1. Track IDs remain V1–V4; only the visible rows are compressed.
- Kept lane heights between 40 and 56 pixels. If all rows do not fit, the track viewport clips and scrolls rather than squeezing them.
- Added Shift + mouse-wheel vertical lane scrolling and smooth vertical hand panning.
- Added middle-mouse temporary hand panning. It does not change the selected tool, so releasing the middle button restores the prior tool automatically.
- Added keyboard shortcuts: V Select, H Hand, C Cut, T Text, Ctrl+Z Undo, Ctrl+Shift+Z Redo, and Ctrl+Y Redo.

## Build and verification

Build with the repository's existing Windows workflow. The current environment does not include the Raylib/Clay headers or the Rust toolchain, so a native compile and Cargo test run could not be performed here.
