# Final Cut-style magnetic timeline — every track

This patch applies one consistent magnetic editing model to every ordinary video and audio lane. It is **not** a V1-only storyline, and it does not make V2–V6 or A1–A6 free-placement tracks.

## Required behavior

- Every video lane (V1–V6) and audio lane (A1–A6) is its own continuous magnetic sequence.
- Deleting or moving a clip closes the vacated interval on the row it came from, even when another row still contains video/audio underneath.
- Inserting or moving into a populated row uses that row's nearest valid cut when there is no valid nearby cross-row snap.
- A cross-row snap is only accepted within the normal proximity threshold **and** when it is also a cut point on the destination row (or the new head before that row's first clip). This prevents a nearby V1 edge from creating a gap in V2.
- Inserting before the first clip on a row is valid; the new clip becomes the row's head and existing clips ripple after it.
- Existing gaps on the affected source and destination rows are compacted; a row never magnetizes to a remote split on another row.
- A collision ripples the destination row in one pass. A grouped edit is rippled once per affected row, avoiding repeated pushes that previously made clips jump/jumble.
- Affected tracks, removed intervals, and linked-follower ripple passes are processed in stable V1→V6 then A1→A6 order rather than HashMap/HashSet iteration order, preventing run-to-run lane-order variation.
- Clip moves keep the chosen lane; backend lane reassignment is not used as a workaround for a collision.
- Linked audio/video and keyframes follow their clip. Linked pairs are not independently compacted during trim, which would break synchronization.
- Titles/text remain on V6 and use the same insertion, gap-close, and ripple rules as other clips.
- Subtitle cues remain on the synthetic S lane and retain their exact cue timestamps.

## Source changes

- Fixed the four reported E0503 borrow-checker errors by calculating keyframe deltas before passing clips as mutable references.
- Added lane-local compaction on insertion, deletion, splitting, trimming, relinking, reattachment, and both source/destination sides of a move.
- Reworked group ripple to calculate a single insertion boundary per row and move followers once.
- Kept proximity snapping bounded and made the frontend drag preview use the same lane-cut projection as the Rust backend.
- Fixed the title refresh fallback JSON in `backend_bridge.h`, so a title missing from the current UI snapshot can be refreshed and selected instead of silently appearing unplaced.

## Regression tests added

- Deleting a V2 clip closes the V2 gap while V1 remains untouched and continues to contain video.
- Moving a clip compacts both the vacated source row and the destination row.
- V6 title insertion closes its own lane gap without moving V1.
- A proximity snap to another row is rejected when it would create a gap in the destination row; inserting before the first destination clip remains valid.
- Existing tests cover short-range cross-row snapping, ignoring distant splits, independent audio rows, V2-to-V3 moves, and title placement on V6.

## Verification status

- Frontend `TimelineSnapStart` and `TimelineMagneticStart` syntax checked with GCC using a minimal stub harness.
- Cargo/Rust is not installed in this environment, so the Rust unit tests were not executed here. Run `cargo test --manifest-path Backend/Cargo.toml -p pluto-timeline-core` in Windows CI before merging.
- Native Windows frontend build is also not executed here.
