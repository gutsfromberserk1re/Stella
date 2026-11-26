# HSL channel selection fix

## Problem
Changing the active HSL color chip could leave the previous chip's slider state active. If the previous slider edit was still in a live property-edit transaction, selecting the next chip used the fast property path, which skipped the full timeline sync and inspector hydration. The shared slider-send cache also did not distinguish between HSL bands.

## Changes
- Commit any pending live property edit before switching HSL chips.
- Refresh the selected HSL index through the normal timeline property path so the inspector hydrates the newly selected band's saved Hue, Saturation, and Lightness values.
- Reseed the slider-send cache whenever the selected clip or HSL band changes, preventing a previous band's progress from being written into the next band.
- Add a backend regression test that stores independent yellow and red HSL values, switches selected bands, and verifies both sets remain unchanged.

## Verification
- Source/archive integrity checks passed.
- Cargo tests and native Windows compilation were not run in this environment because the Rust toolchain and the Windows Raylib/Clay build dependencies are unavailable.
