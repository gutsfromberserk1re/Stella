# Unified magnetic video timeline — V1 through V6

## Behavior

- V1–V6 share one continuous magnetic video sequence. Inserting or moving a clip on any video row chooses a cut in that shared sequence, not a cut local to the destination row.
- Moving or deleting video closes only the uncovered portion of the removed interval across the shared video sequence. If another video row still covers that time, later clips do not ripple for the covered portion.
- Cross-track overlaps remain intentional overlays. Dropping inside another video row preserves the requested time; compaction preserves overlay starts and advances the sequence cursor to the furthest covered end. Only uncovered gaps are closed.
- Collision ripple remains row-local: a clip inserted over another clip on the same physical row pushes that row's followers, while a clip on another video row may overlap as a compositing layer.
- The selected track is always preserved. Magnetic placement never silently reassigns a clip to another row.
- Audio A1–A6 remain independent magnetic sequences. Video edits do not compact or ripple unrelated audio rows; linked audio follows its video partner when that partner moves.
- Subtitle cues remain on the synthetic S lane and retain exact cue timestamps.
- Keyframes move by exactly the same delta as their clip.

## Source changes

- Added a shared magnetic-lane predicate for V1–V6 while keeping audio lanes independent.
- Updated Rust insertion, movement, gap closing, compaction, cut selection, and frontend drag projection to use the shared video sequence.
- Compaction preserves overlaps as overlays instead of stacking every clip sequentially; uncovered gaps are closed deterministically.
- Kept physical-row collision ripple separate from cross-row magnetic sequencing so intentional overlays are not destroyed.
- Added regression tests for cross-track sequence continuity, preserved overlays, and insertion on a different video row.

## Verification

- Frontend magnetic helper passed a standalone Clang C11 syntax check with warnings treated as errors (excluding the expected unused-static-function warning in the isolated harness).
- Rust tests could not be run in this environment because Cargo/Rust is not installed. Run `cargo test --manifest-path Backend/Cargo.toml -p pluto-timeline-core` on the Windows development machine before merging.
- Native Windows frontend build was not run here.
