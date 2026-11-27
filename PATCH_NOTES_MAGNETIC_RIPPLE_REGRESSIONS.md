# Magnetic Timeline Regression Fixes

## Fixed

- Delete ripple now removes only the uncovered portion of the deleted clip's interval. It no longer runs a second full-lane compaction pass that collapses unrelated pre-existing gaps.
- Video delete ripple still evaluates coverage across V1-V6, preserving intentional overlays that cover part of the deleted interval.
- Audio delete ripple remains isolated to the affected audio lane.
- Title clips now use their own destination-track magnetic sequence rather than the shared V1-V6 footage sequence.
- Titles on an empty V6 lane retain their requested playhead position and do not proximity-snap to unrelated lower video cuts.
- Title compaction and title delete/move ripple are scoped to the title's physical track.
- Removed redundant `snapped_to_another_lane = false` assignments that produced compiler warnings.

## Regression tests

- Existing video delete ripple test verifies followers move from 7s to 4s after deleting a 3s interval.
- Existing audio delete ripple test verifies A1 followers move from 7s to 4s while A2 remains unchanged.
- Title placement test now starts at 7.1s, close to a lower-track cut, and verifies it remains at 7.1s.

## Verification

The source changes and ZIP archive were inspected and packaged. Cargo/Rust tooling is not available in this packaging environment, so the Rust test suite has not been executed here. Run:

```bash
cargo test --manifest-path Backend/Cargo.toml -p pluto-timeline-core
```
