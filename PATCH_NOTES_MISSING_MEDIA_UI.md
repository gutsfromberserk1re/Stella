# Missing media indicator and conditional relink action

- Timeline context menus now offer **Relink media…** only when the selected timeline clip has a real source path that no longer exists on disk.
- Generated title clips and subtitle clips are excluded from relink detection; their rendered/generated assets are not user source media.
- Missing media-library items remain visible for identification but are greyed out with a `MISSING` badge and cannot be dragged into the timeline.
- Timeline clips whose source file is missing receive a muted overlay and `MISSING` badge while preserving their timing, duration, selection, and ability to open the context menu for relinking.
- The availability check uses Raylib's `FileExists`, consistent with existing frontend file-path checks.
