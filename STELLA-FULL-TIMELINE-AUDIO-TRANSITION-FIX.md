# Stella full timeline, audio and transition fix

Included changes:
- Magnet-off clip moves preserve free placement and do not close the vacated lane; magnet-on retains magnetic placement/gap closing.
- Transition preview uses native WGPU transition uniforms rather than forcing the FFmpeg graph fallback for ordinary video-video transitions. Incoming-source pre-roll is continuous at the cut; FFmpeg export adds matching cloned-frame pre-roll when a source has no pre-roll handles.
- Timeline audio uses cached OGG playback proxies with MP3 fallback, Raylib streaming voices, fade/volume/pan handling, per-clip failure caching and deleted-clip cleanup.
- Crop inspector remains percent-based and clamps crop to 0–45%; distort retains signed values. Scrub sensitivity is increased.
- Video lanes V1–V4 remain visible as a stable stack, including empty lanes; clip thumbnails, low-opacity drag ghost and transition/fade cones are preserved.
- Audio-only clips keep a muted but saturated blue/cyan gradient with no grid and a white 80%-opacity waveform.
- Text editor/tool was intentionally left unchanged.

Validation performed here: source assertions, merge-marker scan, delimiter checks, transition pre-roll arithmetic checks, and ZIP integrity. A full Windows build and Rust test suite still require the project's GitHub Actions/Rust/MSYS2 toolchain.
