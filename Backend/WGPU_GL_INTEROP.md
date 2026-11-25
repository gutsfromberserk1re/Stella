# Windows WGPU ↔ OpenGL shared-texture preview

## Data path

1. After Raylib creates its OpenGL window, the frontend registers the current WGL context with `pluto_backend_set_gl_share_context`. This must happen before the first backend preview request initializes the process-wide WGPU renderer.
2. Before `wgpu::Instance::new`, the backend passes Raylib's current `HGLRC` and `HDC` to the pinned HAL patch. The WGPU hidden window is positioned on the host window's display, adopts the host DC's pixel-format index/descriptor, and the HAL passes the host `HGLRC` as the share context when it creates WGPU's final WGL context. Sharing therefore happens at context creation, before WGPU allocates its GL resources; it does not rely on a late `wglShareLists` call after Raylib has already created textures and buffers.
3. The compositor renders directly into a WGPU output texture. The HAL texture handle is inspected only to obtain its native `GL_TEXTURE_2D` name; WGPU remains the owner.
4. The backend sends the texture name with a producer-ready GLsync fence. Raylib first checks the handle with `glIsSync`; if visible in the host context, it queues `glWaitSync` before sampling. If not, it calls a backend fallback that finishes the WGPU producer context and deletes the local fence. After `EndDrawing`, Raylib attempts a producer-side server wait using its post-draw GLsync if WGPU can see that sync; if not, Raylib calls `glFinish` before returning the slot.
5. Three output textures allow a displayed frame, a completed frame, and a rendering frame to coexist. After `EndDrawing`, the UI inserts and flushes a fence. The WGPU context queues a server-side wait on that fence before marking the slot reusable; it does not wait for the CPU. If the driver does not share sync objects, the UI uses `glFinish` as the compatibility fallback. If all slots are still owned by the UI, that frame uses asynchronous CPU readback rather than overwriting a displayed texture or stopping playback.

## Build

The minimal HAL accessor is tied to upstream WGPU tag `v0.19.4`. The preparation script path-patches **`wgpu-core`, `wgpu-hal`, and `wgpu-types` together** from that one checkout, and makes the `cfg_aliases` build dependency standalone in both `wgpu-core` and `wgpu-hal`. This is required: patching only HAL mixes its path dependency on local `wgpu-types` with the registry `wgpu-core`/`wgpu-types` crates, producing duplicate Rust types and a cascade of `E0308` errors. The backend pins `wgpu` and `wgpu-hal` to `0.19.4`, and `glow` to the same `0.13.1` native-handle types used by that HAL release. Run this before local Windows Cargo builds; CI already runs it:

```powershell
./Backend/scripts/prepare-wgpu-gl-share.ps1
cargo build --release --manifest-path Backend/Cargo.toml -p pluto-backend-ffi
```

The script creates `Backend/.interop/wgpu-0.19.4` and root `.cargo/config.toml`; both are ignored. It fails if the expected upstream source locations change.

## Fallback and limitations

- Successful WGL share-list setup means preview output avoids GPU readback and CPU-to-GL texture upload. This is the zero-copy portion of the preview presentation path. If producer GLsync is share-visible, readiness is handed off with a GPU-side wait; otherwise the WGPU producer context is finished before publication. The reverse handoff also checks sync visibility in WGPU's context and uses a GPU-side wait when available; `glFinish` is the compatibility fallback when the driver does not share sync handles.
- WGL sharing may still fail if the hidden window resolves to a different display device, the host pixel format cannot be applied to the hidden DC, the driver lacks the required WGL context-creation sharing support, or the driver rejects the share group. The host verifies that the returned texture name is visible in Raylib's GL namespace; if not, Stella disables interop for the rest of that run and requests another frame through asynchronous readback. Set `STELLA_DISABLE_WGPU_GL_INTEROP=1` to force the readback path for diagnosis.
- Non-Windows builds use the readback route. Final export also remains CPU-backed because FFmpeg currently consumes host RGBA frames. Source video decode still produces CPU RGBA that is uploaded to WGPU; this change removes only the final WGPU-output → CPU → OpenGL round trip.
- The F3 overlay's final row identifies the last successfully displayed output as `WGPU-GL ZERO-COPY`, `WGPU CPU readback`, or `FFmpeg fallback`. Confirm the first label with a real Windows run before treating this path as validated.
