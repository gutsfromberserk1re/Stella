# Stella inspector-to-preview property fix

## What was wrong

The C inspector sends fully-qualified paths such as `props.transform.opacity`.
The Rust setter had already removed `clip.extra["props"]` into a local `props`
value, then wrote the whole path beneath it. This produced
`clip.extra["props"]["props"]["transform"]["opacity"]` instead of
`clip.extra["props"]["transform"]["opacity"]`.

The preview renderer and export filters read the latter property tree, so
inspector edits could be accepted by the backend without affecting the rendered
clip.

## What changed

`Backend/backend-ffi/src/lib.rs` now strips an optional leading `props.` before
writing into the extracted property object. Relative paths remain supported.
Regression tests were added for:

- Position X/Y, scale W/H, rotation, and opacity reaching `LayerUniforms`.
- Crop and distort transform properties.
- Color grading and effects properties being stored at the renderer's root.
- Existing relative property paths remaining supported.

No frontend UI or serialized clip schema was changed.

## Verify and build on Windows

From the extracted project root, with Rust/Cargo installed:

```powershell
cargo test --manifest-path Backend/Cargo.toml -p pluto-backend-ffi
cargo build --release --manifest-path Backend/Cargo.toml -p pluto-backend-ffi
```

The repository's Windows packaging workflow in `.github/workflows/thing.yml`
sets up the static FFmpeg decode libraries, LLVM/libclang, the Rust toolchain,
and C build tools required for a packaged Windows build. Use that workflow if
you need a complete Windows package rather than only the backend DLL.

## Verification status

The source-level change and regression tests are included. The Rust tests and
DLL build were not run while preparing this archive because Cargo/Rust are not
installed in the editing environment.
