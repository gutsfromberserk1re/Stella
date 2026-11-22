# Live inspector, crop, and keyframe update

This source update builds on the inspector property-path correction in the working Stella source.

## What changed

- Transform inspector drags (position, scale, rotation, opacity, crop, and distort) send live backend edits while the pointer moves. Live replies contain only an acknowledgement; the frontend performs one full timeline refresh after the gesture.
- The backend captures one undo snapshot at the beginning of a drag and commits it once at the end, rather than cloning the full timeline and recording one undo step per pointer-move event.
- Batched transform edits only clone/update the selected clip. Group siblings are cloned only for group-shared color/effect updates.
- Video transform keyframes (position X/Y, scale W/H, rotation, and opacity) are linearly interpolated in the native WGPU preview path. The inspector follows the current playhead, and editing a property while parked on an existing keyframe updates that keyframe's value.
- Keyframe diamonds are slightly larger, have a larger hit area, and can be dragged along the selected clip. Dragging snaps to the preview frame rate, stays within the clip, and uses one undo transaction.
- Export filter expressions evaluate the same transform keyframes per frame.
- Crop now removes source-edge pixels while retaining the original frame geometry. The remaining image is not scaled up to fill the cropped area. The export path crops the fitted source before restoring it to a transparent canvas, including for letterboxed and pillarboxed footage.

## Keyframe channels

The current transform keyframe channels are `x`, `y`, `scaleW`, `scaleH`, `rotation`, and `opacity`. Keyframe times are absolute timeline seconds. Interpolation is linear and values hold at the first/last keyframe outside the keyed interval.

## Build and test

From the project root on Windows, with the Rust toolchain and the FFmpeg/libclang build dependencies configured:

```powershell
cargo test --manifest-path Backend/Cargo.toml -p pluto-timeline-core
cargo test --manifest-path Backend/Cargo.toml -p pluto-backend-ffi
cargo build --release --manifest-path Backend/Cargo.toml -p pluto-backend-ffi
```

Then build the native UI using the repository's Windows build workflow and place the newly built `pluto_backend.dll` beside `Stella.exe`.

The source update was statically inspected and the generated FFmpeg crop/animated-transform filter chain was exercised against synthetic wide, 4:3, and portrait clips. A Rust test/build could not be run in the authoring environment because Cargo/Rust is not installed there.
