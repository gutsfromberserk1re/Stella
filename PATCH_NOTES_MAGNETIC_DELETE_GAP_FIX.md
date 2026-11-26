# Magnetic delete gap fix

## Fix

- Delete now applies one ripple-delete pass for the exact removed clip interval on each affected lane.
- Removed a redundant full-lane compaction immediately after that pass. The second pass had also removed unrelated pre-existing gaps, shifting followers too far left.
- Video and audio rows remain independently magnetic; deleting on V2 does not affect V1/V3 or other audio rows, and deleting on A1 does not affect A2 or video rows.
- Linked clips are still deleted together, and `close_lane_gaps` keeps linked followers synchronized across their partner lane.

## Regression coverage

The existing `deleting_a_video_clip_closes_only_its_own_lane` and `magnetic_audio_tracks_are_independent_and_close_their_own_gaps` tests cover the failing video/audio cases. They assert that the followers move by the deleted clip duration while unrelated rows remain unchanged.

## Verification

The supplied CI run compiled and executed 40 timeline-core tests, with 38 passing and these two deletion-compaction cases failing. This patch removes the redundant second compaction that caused both failures. Cargo is not available in the patching environment, so the test suite must be rerun in CI to confirm.
