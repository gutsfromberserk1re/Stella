/* Timeline panel. */
/* ------------------------------------------------------------------ */
/* Rust-backed timeline and editing controls                            */
/* ------------------------------------------------------------------ */
static float TimelinePps(void) { return 21.5f * gTimelineZoom; }
static float gTimelineLaneHeight = 56.f;

static bool TimelineTrackIsVideo(int track) { return track >= 0 && track < 4; }

static bool TimelineVideoTrackIsVisible(int track) {
    for (int i = 0; i < gTimelineVideoTracks; i++)
        if (gTimelineVideoTrackIds[i] == track) return true;
    return false;
}

static void TimelineAddVisibleVideoTrack(int track) {
    if (track < 0 || track >= 4 || TimelineVideoTrackIsVisible(track) || gTimelineVideoTracks >= 4) return;
    int insert = gTimelineVideoTracks;
    while (insert > 0 && gTimelineVideoTrackIds[insert - 1] < track) {
        gTimelineVideoTrackIds[insert] = gTimelineVideoTrackIds[insert - 1];
        insert--;
    }
    gTimelineVideoTrackIds[insert] = track;
    gTimelineVideoTracks++;
}

static void TimelineRebuildVisibleVideoTracks(void) {
    gTimelineVideoTracks = 0;
    /* Unused intermediate lanes collapse; V1 is always present. */
    for (int track = 3; track >= 1; track--) {
        bool used = false;
        for (int i = 0; i < gTimelineClipCount; i++)
            if (gTimelineClips[i].track == track) { used = true; break; }
        if (used) TimelineAddVisibleVideoTrack(track);
    }
    TimelineAddVisibleVideoTrack(0);

    /* Keep the source lane mounted for the entire drag gesture. When the
       preview follows the pointer into another lane, rebuilding from the
       moved clip alone would otherwise collapse the source row mid-drag.
       That makes row coordinates jump under the cursor and causes the clip
       to appear stuck on (or snap back to) the original lane. */
    if (gClipDragIndex >= 0 && TimelineTrackIsVideo(gClipDragTrack))
        TimelineAddVisibleVideoTrack(gClipDragTrack);

    /* Keep a temporarily revealed destination lane visible until release. */
    if ((gMediaDragIndex >= 0 || gClipDragIndex >= 0) &&
        gTimelinePendingVideoTrack >= 1 && gTimelinePendingVideoTrack < 4)
        TimelineAddVisibleVideoTrack(gTimelinePendingVideoTrack);
}

static int TimelineTrackUnderPointer(void) {
    Vector2 mouse = GetMousePosition();
    Clay_BoundingBox viewport = Clay_GetElementData(CLAY_ID("TrackViewport")).boundingBox;
    if (viewport.width > 0.f && !CheckCollisionPointRec(mouse,
        (Rectangle){viewport.x, viewport.y, viewport.width, viewport.height})) return -1;
    float best = INFINITY;
    int bestTrack = -1;
    for (int i = 0; i < gTimelineVideoTracks; i++) {
        int track = gTimelineVideoTrackIds[i];
        Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", track)).boundingBox;
        float center = lane.y + lane.height * .5f;
        float distance = fabsf(mouse.y - center);
        if (lane.width > 0.f && mouse.x >= lane.x - 4.f && mouse.x <= lane.x + lane.width + 4.f &&
            distance <= lane.height * .5f + fmaxf(8.f, gTimelineLaneHeight * .25f) && distance < best) {
            best = distance; bestTrack = track;
        }
    }
    for (int track = 4; track < 4 + gTimelineAudioTracks; track++) {
        Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", track)).boundingBox;
        float center = lane.y + lane.height * .5f;
        float distance = fabsf(mouse.y - center);
        if (lane.width > 0.f && mouse.x >= lane.x - 4.f && mouse.x <= lane.x + lane.width + 4.f &&
            distance <= lane.height * .5f + fmaxf(8.f, gTimelineLaneHeight * .25f) && distance < best) {
            best = distance; bestTrack = track;
        }
    }
    return bestTrack;
}

static float TimelineSnapStart(float start, float duration, bool video, int movingIndex, float playhead, bool snapPlayhead, const char *excludeGroup) {
    if (!video) return fmaxf(0.f, start);

    /* Magnetic video placement is always on. The magnet button controls only
       optional proximity snapping to nearby boundaries and the playhead. */
    float nearestLeft = -1.f;
    for (int n = 0; n < gTimelineClipCount; n++) {
        if (n == movingIndex || !TimelineTrackIsVideo(gTimelineClips[n].track) ||
            (excludeGroup && *excludeGroup && strcmp(gTimelineClips[n].groupId, excludeGroup) == 0)) continue;
        float otherEnd = gTimelineClips[n].start + gTimelineClips[n].duration;
        if (otherEnd <= start + .0001f && otherEnd > nearestLeft) nearestLeft = otherEnd;
    }
    float magnetic = nearestLeft >= 0.f ? nearestLeft : 0.f;
    if (!gTimelineSnap) return magnetic;

    float bestDistance = .25f, snapped = magnetic;
    for (int n = -1; n < gTimelineClipCount; n++) {
        if (n == movingIndex || (n >= 0 && (!TimelineTrackIsVideo(gTimelineClips[n].track) ||
            (excludeGroup && *excludeGroup && strcmp(gTimelineClips[n].groupId, excludeGroup) == 0)))) continue;
        float candidates[2]; int count = 0;
        if (n < 0) { if (snapPlayhead) candidates[count++] = playhead; }
        else { candidates[count++] = gTimelineClips[n].start; candidates[count++] = gTimelineClips[n].start + gTimelineClips[n].duration; }
        for (int c = 0; c < count; c++) for (int edge = 0; edge < 2; edge++) {
            float value = candidates[c] - (edge ? duration : 0.f);
            float distance = fabsf(value - magnetic);
            if (distance < bestDistance) { bestDistance = distance; snapped = fmaxf(0.f, value); }
        }
    }
    return snapped;
}

static bool TimelineTrackOverlaps(int track, float start, float duration, int excluded, const char *excludeGroup) {
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (i == excluded || (excludeGroup && *excludeGroup && strcmp(gTimelineClips[i].groupId, excludeGroup) == 0) ||
            gTimelineClips[i].track != track) continue;
        if (start < gTimelineClips[i].start + gTimelineClips[i].duration - .0001f &&
            start + duration > gTimelineClips[i].start + .0001f) return true;
    }
    return false;
}

static int TimelineClosestFreeTrack(int requested, float start, float duration, int excluded, const char *excludeGroup) {
    int first = TimelineTrackIsVideo(requested) ? 0 : 4;
    int last = TimelineTrackIsVideo(requested) ? 3 : 5;
    int best = requested, bestDistance = 99;
    for (int track = first; track <= last; track++) {
        int distance = abs(track - requested);
        bool tiePrefersHigherVideoLane = distance == bestDistance && TimelineTrackIsVideo(track) && track > best;
        if ((distance < bestDistance || tiePrefersHigherVideoLane) && !TimelineTrackOverlaps(track, start, duration, excluded, excludeGroup)) {
            best = track; bestDistance = distance;
        }
    }
    return best;
}

static void TimelinePreviewClipMove(int index, float start, int track);

static void TimelineSyncLinkedDragPreview(Vector2 mouse, float pps) {
    if (gClipDragIndex < 0 || gClipDragIndex >= gTimelineClipCount ||
        !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;

    TimelineClipUi *clip = &gTimelineClips[gClipDragIndex];
    if (!clip->linkedId[0] && !clip->groupId[0]) return;

    int hoveredTrack = TimelineTrackUnderPointer();
    int requestedTrack = hoveredTrack >= 0 &&
        TimelineTrackIsVideo(hoveredTrack) == TimelineTrackIsVideo(gClipDragTrack)
        ? hoveredTrack : gClipDragTrack;
    Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", requestedTrack)).boundingBox;
    if (lane.width <= 0.f) return;

    float start = fmaxf(0.f, (mouse.x - lane.x - gTimelinePan - gClipDragOffset) / pps);
    start = TimelineSnapStart(start, clip->duration, TimelineTrackIsVideo(gClipDragTrack), gClipDragIndex,
                              gPlayhead, true, clip->groupId[0] ? clip->groupId : NULL);
    int targetTrack = TimelineClosestFreeTrack(requestedTrack, start, clip->duration, gClipDragIndex,
                                                clip->groupId[0] ? clip->groupId : NULL);
    TimelinePreviewClipMove(gClipDragIndex, start, targetTrack);
}

static void TimelineRestoreDragSnapshot(void) {
    if (gClipDragIndex < 0 || gClipDragIndex >= gTimelineClipCount) return;
    for (int i = 0; i < gTimelineClipCount; i++) {
        gTimelineClips[i].start = gClipDragSnapshotStart[i];
        gTimelineClips[i].track = gClipDragSnapshotTrack[i];
    }
}

static void TimelinePreviewClipMove(int index, float start, int track) {
    if (index < 0 || index >= gTimelineClipCount) return;
    TimelineRestoreDragSnapshot();
    TimelineClipUi *clip = &gTimelineClips[index];
    const char *groupId = clip->groupId[0] ? clip->groupId : NULL;
    float delta = start - gClipDragSnapshotStart[index];
    int trackDelta = track - gClipDragSnapshotTrack[index];
    int moving[MAX_TIMELINE_CLIPS], movingCount = 0;
    for (int i = 0; i < gTimelineClipCount; i++) {
        bool sameGroup = groupId && strcmp(gTimelineClips[i].groupId, groupId) == 0;
        bool linked = strcmp(gTimelineClips[i].id, clip->linkedId) == 0 ||
                      strcmp(gTimelineClips[i].linkedId, clip->id) == 0;
        if (i == index || sameGroup || linked) moving[movingCount++] = i;
    }

    /* Compact each video lane by the durations of all group members it loses. */
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (!TimelineTrackIsVideo(gClipDragSnapshotTrack[i])) continue;
        bool isMoving = false;
        for (int m = 0; m < movingCount; m++) if (moving[m] == i) { isMoving = true; break; }
        if (isMoving) continue;
        float shifted = gClipDragSnapshotStart[i];
        int laneMoves[MAX_TIMELINE_CLIPS], laneMoveCount = 0;
        for (int m = 0; m < movingCount; m++) {
            int moved = moving[m];
            if (TimelineTrackIsVideo(gClipDragSnapshotTrack[moved]) &&
                gClipDragSnapshotTrack[moved] == gClipDragSnapshotTrack[i]) laneMoves[laneMoveCount++] = moved;
        }
        for (int a = 1; a < laneMoveCount; a++) {
            int value = laneMoves[a], b = a;
            while (b > 0 && gClipDragSnapshotStart[laneMoves[b - 1]] > gClipDragSnapshotStart[value]) {
                laneMoves[b] = laneMoves[b - 1]; b--;
            }
            laneMoves[b] = value;
        }
        for (int m = 0; m < laneMoveCount; m++) {
            int moved = laneMoves[m];
            float movedStart = gClipDragSnapshotStart[moved];
            if (shifted >= movedStart + gTimelineClips[moved].duration - .001f)
                shifted = fmaxf(movedStart, shifted - gTimelineClips[moved].duration);
        }
        gTimelineClips[i].start = shifted;
    }

    for (int m = 0; m < movingCount; m++) {
        int moved = moving[m];
        gTimelineClips[moved].start = fmaxf(0.f, gClipDragSnapshotStart[moved] + delta);
        if (TimelineTrackIsVideo(gClipDragSnapshotTrack[moved]) == TimelineTrackIsVideo(gClipDragSnapshotTrack[index])) {
            int first = TimelineTrackIsVideo(gClipDragSnapshotTrack[moved]) ? 0 : 4;
            int last = TimelineTrackIsVideo(gClipDragSnapshotTrack[moved]) ? 3 : 5;
            gTimelineClips[moved].track = moved == index ? track :
                (int)fmaxf((float)first, fminf((float)last, (float)(gClipDragSnapshotTrack[moved] + trackDelta)));
        }
    }
}

static const char *TimelineTrackLabel(int track) {
    static const char *labels[] = {"V1", "V2", "V3", "V4", "A1", "A2"};
    return labels[track < 0 ? 0 : track > 5 ? 5 : track];
}

static void TimelineClipLane(int track);

static void TimelineTrackRow(int track, float height) {
    Clay_ElementId row = CLAY_IDI("TrackRow", track);
    static const Color videoBands[3] = {{26, 27, 30, 255}, {25, 26, 29, 255}, {30, 30, 32, 255}};
    static const Color audioBands[2] = {{23, 24, 27, 255}, {26, 27, 30, 255}};
    Color bgRaylib = track < 4 ? videoBands[track % 3] : audioBands[(track - 4) % 2];
    /* FIX: Clay's backgroundColor is a Clay_Color (floats), not a raylib Color (unsigned char). */
    Clay_Color bg = (Clay_Color){(float)bgRaylib.r, (float)bgRaylib.g, (float)bgRaylib.b, (float)bgRaylib.a};
    CLAY(row, {.layout = {.sizing = {GROW, FIX(height)}, .childAlignment = AL_Y}, .backgroundColor = bg,
               .border = {.color = RGB(41, 42, 46), .width = {.bottom = 1}}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
            Icon(track < 4 ? I_FILM : I_NOTE, 14, C_MUTED);
            TxMedium(TimelineTrackLabel(track), 13, C_MUTED);
        }
        TimelineClipLane(track);
    }
}

static float TimelineDuration(void) {
    float duration = 0.f;
    for (int i = 0; i < gTimelineClipCount; i++)
        duration = fmaxf(duration, gTimelineClips[i].start + gTimelineClips[i].duration);
    return duration > 0.f ? duration : 9.f;
}

static int TimelineKeyframeHit(int clipIndex, Clay_BoundingBox bounds, Vector2 mouse) {
    if (clipIndex < 0 || clipIndex >= gTimelineClipCount || bounds.width <= 0.f) return -1;
    TimelineClipUi *clip = &gTimelineClips[clipIndex];
    for (int k = 0; k < clip->keyframeCount; k++) {
        float time = clip->keyframes[k].time;
        if (time < clip->start - .001f || time > clip->start + clip->duration + .001f) continue;
        float x = bounds.x + (time - clip->start) / fmaxf(.001f, clip->duration) * bounds.width;
        float y = bounds.y + 14.f;
        if (fabsf(mouse.x - x) <= 12.f && fabsf(mouse.y - y) <= 14.f) return k;
    }
    return -1;
}

static bool TimelineKeyframeAtPointer(Vector2 mouse) {
    for (int i = 0; i < gTimelineClipCount; i++) {
        Clay_BoundingBox bounds = Clay_GetElementData(CLAY_IDI("TimelineClip", i)).boundingBox;
        if (TimelineKeyframeHit(i, bounds, mouse) >= 0) return true;
    }
    return false;
}

static int TimelineClipStartCompare(const void *left, const void *right) {
    int a = *(const int *)left, b = *(const int *)right;
    float delta = gTimelineClips[a].start - gTimelineClips[b].start;
    if (delta < -0.000001f) return -1;
    if (delta > 0.000001f) return 1;
    return a < b ? -1 : a > b ? 1 : 0;
}

static void TimelineClipLane(int track) {
    Clay_ElementId laneId = CLAY_IDI("TrackLane", track);
    Clay_BoundingBox lane = Clay_GetElementData(laneId).boundingBox;
    float pps = TimelinePps();
    if (gTimelineTool == 2 && Clay_PointerOver(laneId)) gCross = true;
    bool transitionPressed = false;
    int order[MAX_TIMELINE_CLIPS], count = 0;
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (gTimelineClips[i].track == track) order[count++] = i;
    }
    /* qsort avoids the old insertion-sort O(n^2) cost when large timelines
       are edited or redrawn. The index tie-break keeps ordering deterministic. */
    if (count > 1) qsort(order, (size_t)count, sizeof(order[0]), TimelineClipStartCompare);

    /* Mirror Clay's horizontal flow when tiny clips hit the readable 46px
       minimum. Transition badges use these displayed edges, not compressed
       timeline time positions, so they stay centered at low zoom. */
    float visualLeft[MAX_TIMELINE_CLIPS] = {0};
    float visualWidth[MAX_TIMELINE_CLIPS] = {0};
    float visualCursor = 0.f;
    for (int n = 0; n < count; n++) {
        int i = order[n];
        float x = gTimelinePan + gTimelineClips[i].start * pps;
        float width = fmaxf(46.f, gTimelineClips[i].duration * pps);
        if (lane.width > 0.f && x >= lane.width) break; /* sorted: every later clip is also off-screen */
        if (x + width <= 0.f) continue;
        float left = fmaxf(0.f, x);
        float placed = fmaxf(visualCursor, left);
        if (x < 0.f) width = fmaxf(1.f, width + x);
        visualLeft[i] = lane.x + placed;
        visualWidth[i] = width;
        visualCursor = placed + width;
    }

    if (track < 4) {
        for (int n = 0; n + 1 < count && gTransitionButtonCount < 64; n++) {
            int left = order[n], right = order[n + 1];
            float end = gTimelineClips[left].start + gTimelineClips[left].duration;
            if (fabsf(end - gTimelineClips[right].start) > .04f) continue;
            if (visualWidth[left] <= 0.f || visualWidth[right] <= 0.f) continue;
            float leftEdge = visualLeft[left];
            float leftVisibleEnd = leftEdge + visualWidth[left];
            float rightEdge = visualLeft[right];
            float rightVisibleEnd = rightEdge + visualWidth[right];
            float joinX = (leftVisibleEnd + rightEdge) * .5f;
            float spanSeconds = fminf(gTimelineClips[left].transitionDuration,
                                      fminf(gTimelineClips[left].duration, gTimelineClips[right].duration));
            float spanPixels = fmaxf(2.f, spanSeconds * pps);
            bool applied = gTimelineClips[left].transitionType[0] &&
                           strcmp(gTimelineClips[left].transitionType, "none") != 0;
            float spanStart = fmaxf(lane.x, fmaxf(leftEdge, joinX - spanPixels * .5f));
            float spanEnd = fminf(lane.x + lane.width, fminf(rightVisibleEnd, joinX + spanPixels * .5f));
            Rectangle durationRect = {spanStart,
                                      lane.y + 5.f,
                                      fmaxf(0.f, spanEnd - spanStart),
                                      5.f};
            Rectangle rect = {joinX - 12.f, lane.y + (lane.height - 24.f) * .5f, 24.f, 24.f};
            bool hovered = CheckCollisionPointRec(GetMousePosition(), rect);
            if (hovered) gHand = true;
            gTransitionButtons[gTransitionButtonCount++] = (TransitionButton){rect, durationRect, left, applied};
            if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                transitionPressed = true;
                gTransitionFocusClip = left;
                gRightTab = 2;
                PlutoTimelineSelect(left);
            }
        }
    }

    CLAY(laneId, {.layout = {.sizing = {GROW, FIX(gTimelineLaneHeight)}, .childGap = 0}, .clip = {.horizontal = true}}) {
        int mediaTarget = TimelineTrackUnderPointer();
        if (gMediaDragIndex >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mediaTarget >= 0 &&
            mediaTarget == track && gMediaDragIndex < gImportedCount) {
            ImportedMedia *media = &gImported[gMediaDragIndex];
            bool mediaIsAudio = strcmp(media->kind, "audio") == 0;
            if (mediaIsAudio == !TimelineTrackIsVideo(track)) {
                float start = fmaxf(0.f, (GetMousePosition().x - lane.x - gTimelinePan) / pps);
                start = TimelineSnapStart(start, media->duration, !mediaIsAudio, -1, 0.f, false, NULL);
                int target = TimelineClosestFreeTrack(track, start, media->duration, -1, NULL);
                if (target < 4 && !TimelineVideoTrackIsVisible(target)) gTimelinePendingVideoTrack = target;
                if (target >= 4 && gTimelineAudioTracks < target - 3) gTimelineAudioTracks = target - 3;
                Clay_BoundingBox targetBox = Clay_GetElementData(CLAY_IDI("TrackLane", target)).boundingBox;
                float x = targetBox.x + gTimelinePan + start * pps;
                float w = fmaxf(46.f, media->duration * pps);
                float left = fmaxf(targetBox.x, x), right = fminf(targetBox.x + targetBox.width, x + w);
                if (right > left) {
                    float h = fminf(48.f, targetBox.height - 6.f);
                    gMediaDropPreview = (Rectangle){left, targetBox.y + (targetBox.height - h) * .5f, right - left, h};
                    gMediaDropPreviewVisible = true;
                    gHand = true;
                }
            }
        }
        float cursor = 0.f;
        for (int n = 0; n < count; n++) {
            int i = order[n];
            float x = gTimelinePan + gTimelineClips[i].start * pps;
            float width = fmaxf(46.f, gTimelineClips[i].duration * pps);
            if (lane.width > 0.f && x >= lane.width) break; /* don't create Clay nodes for invisible clips */
            if (x + width <= 0.f) continue;
            float left = fmaxf(0.f, x);
            if (left > cursor) {
                CLAY_AUTO_ID({.layout = {.sizing = {CLAY_SIZING_FIXED(left - cursor), FIX(1)}}}) {}
                cursor = left;
            }
            if (x < 0.f) width = fmaxf(1.f, width + x);
            Clay_ElementId clipId = CLAY_IDI("TimelineClip", i);
            Bs b = Interact(clipId);
            Clay_BoundingBox bb = Clay_GetElementData(clipId).boundingBox;
            Vector2 pointer = GetMousePosition();
            bool keyframePressed = false;
            if (!transitionPressed && gTimelineTool == 0 && !gTimelineScrubbing &&
                !gTimelineContextConsumedClick && gKeyframeDragClip < 0 &&
                Clay_PointerOver(clipId) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int hitKeyframe = TimelineKeyframeHit(i, bb, pointer);
                if (hitKeyframe >= 0) {
                    keyframePressed = true;
                    gTransitionFocusClip = -1;
                    PlutoTimelineSelect(i);
                    gKeyframeDragClip = i;
                    gKeyframeDragIndex = hitKeyframe;
                    gKeyframeDragTime = gTimelineClips[i].keyframes[hitKeyframe].time;
                    gPlayhead = gKeyframeDragTime;
                    gPreviewDirty = true;
                    gPreviewDebounce = 0.f;
                }
            }
            if (gKeyframeDragClip == i && gKeyframeDragIndex >= 0 &&
                (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
                float rawTime = gTimelineClips[i].start +
                    (pointer.x - bb.x) / fmaxf(1.f, bb.width) * gTimelineClips[i].duration;
                float frameRate = (float)fmaxf(1, gPreviewFps);
                float nextTime = roundf(rawTime * frameRate) / frameRate;
                nextTime = fmaxf(gTimelineClips[i].start,
                    fminf(gTimelineClips[i].start + gTimelineClips[i].duration, nextTime));
                if (fabsf(nextTime - gKeyframeDragTime) > 0.0005f) {
                    if (PlutoTimelineMoveKeyframe(i, gKeyframeDragTime, nextTime, true)) {
                        gKeyframeDragTime = nextTime;
                        gPlayhead = nextTime;
                        PlutoApplyKeyframeValues();
                        gPreviewDirty = true;
                        gPreviewDebounce = 0.f;
                    }
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    gKeyframeDragClip = -1;
                    gKeyframeDragIndex = -1;
                }
            }
            if (!keyframePressed && gKeyframeDragClip < 0 && !transitionPressed && !gTimelineScrubbing && !gTimelineContextConsumedClick &&
                Clay_PointerOver(clipId) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                gTransitionFocusClip = -1;
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    gTimelineMultiSelect[i] = true;
                    gTimelineSelectionAnchor = i;
                } else {
                    memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                    gTimelineMultiSelect[i] = true;
                    gTimelineSelectionAnchor = i;
                }
                PlutoTimelineSelect(i);
                if (gTimelineTool == 0 || gTimelineTool == 1) {
                    gClipDragIndex = i;
                    gClipDragTrack = track;
                    gClipDragOrigStart = gTimelineClips[i].start;
                    gClipDragOffset = GetMousePosition().x - bb.x;
                    for (int j = 0; j < gTimelineClipCount; j++) {
                        gClipDragSnapshotStart[j] = gTimelineClips[j].start;
                        gClipDragSnapshotTrack[j] = gTimelineClips[j].track;
                    }
                }
            }
            if (gKeyframeDragClip < 0 && Clay_PointerOver(clipId) && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                if (!gTimelineMultiSelect[i]) {
                    memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                    gTimelineMultiSelect[i] = true;
                }
                PlutoTimelineSelect(i);
                gTimelineContextClip = i;
                gTimelineContextPos = GetMousePosition();
                gTimelineContextOpen = true;
            }
            if (gKeyframeDragClip < 0 && gClipDragIndex == i && (gTimelineTool == 0 || gTimelineTool == 1)) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && lane.width > 0.f) {
                    TimelineRestoreDragSnapshot();
                    int hoveredTrack = TimelineTrackUnderPointer();
                    int requestedTrack = hoveredTrack >= 0 && TimelineTrackIsVideo(hoveredTrack) == TimelineTrackIsVideo(gClipDragTrack)
                        ? hoveredTrack : gClipDragTrack;
                    Clay_BoundingBox targetLane = Clay_GetElementData(CLAY_IDI("TrackLane", requestedTrack)).boundingBox;
                    float start = fmaxf(0.f, (GetMousePosition().x - targetLane.x - gTimelinePan - gClipDragOffset) / pps);
                    start = TimelineSnapStart(start, gTimelineClips[i].duration, TimelineTrackIsVideo(gClipDragTrack), i, gPlayhead, true,
                                              gTimelineClips[i].groupId[0] ? gTimelineClips[i].groupId : NULL);
                    int targetTrack = TimelineClosestFreeTrack(requestedTrack, start, gTimelineClips[i].duration, i,
                                                               gTimelineClips[i].groupId[0] ? gTimelineClips[i].groupId : NULL);
                    if (targetTrack >= 4 && gTimelineAudioTracks < targetTrack - 3) gTimelineAudioTracks = targetTrack - 3;
                    Clay_BoundingBox targetBox = Clay_GetElementData(CLAY_IDI("TrackLane", targetTrack)).boundingBox;
                    float x = targetBox.x + gTimelinePan + start * pps;
                    float w = fmaxf(46.f, gTimelineClips[i].duration * pps);
                    float left = fmaxf(targetBox.x, x), right = fminf(targetBox.x + targetBox.width, x + w);
                    if (right > left) {
                        float h = fminf(48.f, targetBox.height - 6.f);
                        gMediaDropPreview = (Rectangle){left, targetBox.y + (targetBox.height - h) * .5f, right - left, h};
                        gMediaDropPreviewVisible = true;
                        gHand = true;
                    }
                    TimelinePreviewClipMove(i, start, targetTrack);
                } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    TimelineRestoreDragSnapshot();
                    int hoveredTrack = TimelineTrackUnderPointer();
                    int requestedTrack = hoveredTrack >= 0 && TimelineTrackIsVideo(hoveredTrack) == TimelineTrackIsVideo(gClipDragTrack)
                        ? hoveredTrack : gClipDragTrack;
                    Clay_BoundingBox targetLane = Clay_GetElementData(CLAY_IDI("TrackLane", requestedTrack)).boundingBox;
                    float start = targetLane.width > 0.f
                        ? fmaxf(0.f, (GetMousePosition().x - targetLane.x - gTimelinePan - gClipDragOffset) / pps)
                        : gTimelineClips[i].start;
                    start = TimelineSnapStart(start, gTimelineClips[i].duration, TimelineTrackIsVideo(gClipDragTrack), i, gPlayhead, true,
                                              gTimelineClips[i].groupId[0] ? gTimelineClips[i].groupId : NULL);
                    int targetTrack = TimelineClosestFreeTrack(requestedTrack, start, gTimelineClips[i].duration, i,
                                                               gTimelineClips[i].groupId[0] ? gTimelineClips[i].groupId : NULL);
                    if (!PlutoTimelineMove(i, start, targetTrack)) {
                        gTimelineClips[i].start = gClipDragOrigStart;
                        gTimelineClips[i].track = gClipDragTrack;
                        PlutoTimelineCall("{\"command\":\"timeline_get\"}");
                    }
                    gClipDragIndex = -1;
                    gTimelinePendingVideoTrack = -1;
                    gMediaDropPreviewVisible = false;
                }
            }
            if (b.clicked && gTimelineTool == 2) {
                float cutAt = (GetMousePosition().x - lane.x - gTimelinePan) / pps;
                cutAt = fmaxf(gTimelineClips[i].start + .001f,
                              fminf(gTimelineClips[i].start + gTimelineClips[i].duration - .001f, cutAt));
                PlutoTimelineSplit(i, cutAt);
            }
            float cardWidth = fmaxf(1.f, width);
            bool clipSelected = gTimelineSelected == i || gTimelineMultiSelect[i];
            CLAY(clipId, {.layout = {.sizing = {CLAY_SIZING_FIXED(cardWidth), FIX(fminf(48.f, lane.height - 6.f))}, .padding = {8, 5, 5, 0}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_TOP}},
                          .clip = {.horizontal = true, .vertical = true},
                          .custom = {.customData = NC(K_CLIP, RGB(112, 52, 140), RGB(78, 36, 108), C_CARD, 0, b.hover,
                                                     (i << 1) | (clipSelected ? 1 : 0))}}) {
                if (TimelineTrackIsVideo(gTimelineClips[i].track)) {
                    Icon(I_FILM, 13, C_TEXT);
                    if (cardWidth >= 92.f) Tx(gTimelineClips[i].label, 13, C_TEXT, 1);
                }
            }
            cursor = fmaxf(cursor, left + width);
        }
        HSPACE
    }
}

static int TimelineContextActions(int actions[4]) {
    if (gTimelineContextClip < 0 || gTimelineContextClip >= gTimelineClipCount) return 0;
    TimelineClipUi *clip = &gTimelineClips[gTimelineContextClip];
    int count = 0;
    actions[count++] = clip->groupId[0] ? 1 : 0; /* 0 group, 1 ungroup */
    if (TimelineTrackIsVideo(clip->track) && clip->hasAudio) actions[count++] = 2;
    if (clip->linkedId[0]) actions[count++] = 3;
    actions[count++] = 4; /* delete */
    return count;
}

static const char *TimelineContextLabel(int action, int selectedCount) {
    switch (action) {
    case 0: return selectedCount > 1 ? TextFormat("Group %d clips", selectedCount) : "Group selected clips";
    case 1: return "Ungroup clips";
    case 2: return "Separate audio";
    case 3: return "Unlink audio";
    default: return "Delete clip";
    }
}

static Rectangle TimelineContextBounds(void) {
    int actions[4], count = TimelineContextActions(actions);
    float width = 188.f, height = count * 30.f + 8.f;
    float x = gTimelineContextPos.x, y = gTimelineContextPos.y;
    if (x + width > GetScreenWidth() - 8.f) x = GetScreenWidth() - width - 8.f;
    if (y + height > GetScreenHeight() - 8.f) y = GetScreenHeight() - height - 8.f;
    if (x < 8.f) x = 8.f;
    if (y < 8.f) y = 8.f;
    return (Rectangle){x, y, width, height};
}

static void TimelineContextLogic(void) {
    gTimelineContextConsumedClick = false;
    if (!gTimelineContextOpen) return;
    if (gTimelineContextClip < 0 || gTimelineContextClip >= gTimelineClipCount) { gTimelineContextOpen = false; return; }
    Rectangle menu = TimelineContextBounds();
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, menu)) {
            int actions[4], count = TimelineContextActions(actions);
            int row = (int)((mouse.y - menu.y - 4.f) / 30.f);
            if (row >= 0 && row < count) {
                int action = actions[row];
                if (action == 0) {
                    int selected = 0; for (int i = 0; i < gTimelineClipCount; i++) selected += gTimelineMultiSelect[i] ? 1 : 0;
                    if (selected > 1) PlutoTimelineGroupSelected();
                } else if (action == 1) PlutoTimelineUngroup(gTimelineContextClip);
                else if (action == 2) PlutoTimelineSeparateAudio(gTimelineContextClip);
                else if (action == 3) PlutoTimelineUnlink(gTimelineContextClip);
                else if (action == 4) {
                    PlutoTimelineDelete(gTimelineContextClip);
                    memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                    gTimelineSelected = -1;
                }
            }
            gTimelineContextOpen = false;
            gTimelineContextConsumedClick = true;
        } else {
            gTimelineContextOpen = false;
        }
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !CheckCollisionPointRec(mouse, menu)) {
        gTimelineContextOpen = false;
    }
}

static void DrawTimelineContextMenu(void) {
    if (!gTimelineContextOpen) return;
    int actions[4], count = TimelineContextActions(actions);
    Rectangle menu = TimelineContextBounds();
    Vector2 mouse = GetMousePosition();
    gTimelineContextRect = menu;
    if (CheckCollisionPointRec(mouse, menu)) gHand = true;
    FillRR(menu, 9.f, (Color){25, 26, 30, 250});
    StrokeRR(menu, 9.f, 1.f, (Color){54, 57, 65, 255});
    int selected = 0; for (int i = 0; i < gTimelineClipCount; i++) selected += gTimelineMultiSelect[i] ? 1 : 0;
    for (int row = 0; row < count; row++) {
        Rectangle item = {menu.x + 4.f, menu.y + 4.f + row * 30.f, menu.width - 8.f, 30.f};
        bool hover = CheckCollisionPointRec(mouse, item);
        if (hover) FillRR(item, 6.f, (Color){41, 43, 50, 255});
        bool disabled = actions[row] == 0 && selected < 2;
        Color text = disabled ? (Color){111, 114, 122, 255} : actions[row] == 4 ? (Color){233, 112, 118, 255} : (Color){224, 226, 231, 255};
        TxtAt(0, TimelineContextLabel(actions[row], selected), item.x + 11.f, item.y + 6.f, 13.f, text);
    }
}

static void Timeline(void) {
    gTransitionButtonCount = 0;
    TimelineContextLogic();
    gMediaDropPreviewVisible = false;
    TimelineRebuildVisibleVideoTracks();
    float pps = TimelinePps();
    Vector2 mouse = GetMousePosition();
    Clay_BoundingBox timelineBounds = Clay_GetElementData(CLAY_ID("Timeline")).boundingBox;
    Clay_BoundingBox body = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox;
    Clay_BoundingBox ruler = Clay_GetElementData(CLAY_ID("Ruler")).boundingBox;

    /* Reveal one empty video lane only while a video clip/video asset is being dragged. */
    bool draggingVideoItem = gClipDragIndex >= 0 && TimelineTrackIsVideo(gClipDragTrack);
    if (gMediaDragIndex >= 0 && gMediaDragIndex < gImportedCount)
        draggingVideoItem = draggingVideoItem || strcmp(gImported[gMediaDragIndex].kind, "audio") != 0;
    if (draggingVideoItem && gTimelineVideoTracks < 4) {
        Clay_BoundingBox topLane = Clay_GetElementData(CLAY_IDI("TrackLane", gTimelineVideoTrackIds[0])).boundingBox;
        if (topLane.width > 0.f && mouse.y < topLane.y + 2.f) {
            /* Reveal the next higher numbered video lane, not simply the
               first missing ID. Track IDs increase upward (V1 is bottom),
               so this keeps V3 above V2 and V4 above V3 even when lanes
               have been collapsed. More than one lane can be revealed in
               a single drag by continuing to move above the new top row. */
            int candidate = gTimelineVideoTrackIds[0] + 1;
            while (candidate < 4 && TimelineVideoTrackIsVisible(candidate)) candidate++;
            if (candidate < 4) {
                TimelineAddVisibleVideoTrack(candidate);
                gTimelinePendingVideoTrack = candidate;
            }
        }
    }
    TimelineSyncLinkedDragPreview(mouse, pps);

    float playheadX = ruler.x + gTimelinePan + gPlayhead * pps;
    bool grabbedPlayhead = mouse.y >= ruler.y && mouse.y <= body.y + body.height && fabsf(mouse.x - playheadX) <= 9.f;
    bool overKeyframe = TimelineKeyframeAtPointer(mouse);
    if (!overKeyframe && (Clay_PointerOver(CLAY_ID("Ruler")) || grabbedPlayhead) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gTimelineScrubbing = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gTimelineScrubbing = false;
    if (gTimelineScrubbing) {
        Clay_BoundingBox bb = Clay_GetElementData(CLAY_ID("Ruler")).boundingBox;
        if (bb.width > 0.f) {
            float seconds = fmaxf(0.f, (mouse.x - bb.x - gTimelinePan) / pps);
            { const float scrubFps = (float)fmaxf(1, gPreviewFps); gPlayhead = floorf(seconds * scrubFps + .5f) / scrubFps; }
            gPreviewDirty = true;
        }
    }

    if (gMediaDragIndex >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        int targetTrack = TimelineTrackUnderPointer();
        if (targetTrack >= 0 && gMediaDragIndex < gImportedCount) {
            ImportedMedia *media = &gImported[gMediaDragIndex];
            bool audioTrack = targetTrack >= 4;
            if (audioTrack == (strcmp(media->kind, "audio") == 0)) {
                Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", targetTrack)).boundingBox;
                float start = lane.width > 0.f ? fmaxf(0.f, (mouse.x - lane.x - gTimelinePan) / pps) : 0.f;
                start = TimelineSnapStart(start, media->duration, !audioTrack, -1, 0.f, false, NULL);
                targetTrack = TimelineClosestFreeTrack(targetTrack, start, media->duration, -1, NULL);
                PlutoTimelineAddMedia(gMediaDragIndex, start, targetTrack);
            }
        }
        gMediaDragIndex = -1;
        gTimelinePendingVideoTrack = -1;
    }

    /* Middle mouse is a temporary hand tool. It never changes the selected tool. */
    bool inTimelineBody = body.width > 0.f && CheckCollisionPointRec(mouse, (Rectangle){body.x, body.y, body.width, body.height});
    bool inTimelinePanel = timelineBounds.width > 0.f && CheckCollisionPointRec(mouse,
        (Rectangle){timelineBounds.x, timelineBounds.y, timelineBounds.width, timelineBounds.height});
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && inTimelinePanel) {
        gTimelineMiddlePanning = true;
        gTimelinePanMouse = mouse.x;
        gTimelinePanMouseY = mouse.y;
        gTimelinePanOrig = gTimelinePan;
        gTimelineVerticalPanOrig = gTimelineVerticalScrollT;
    }
    if (gTimelineMiddlePanning && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        gTimelinePan = fminf(0.f, gTimelinePanOrig + mouse.x - gTimelinePanMouse);
        gTimelineVerticalScrollT = gTimelineVerticalPanOrig - (mouse.y - gTimelinePanMouseY);
        gHand = true;
    }
    if (gTimelineMiddlePanning && IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) gTimelineMiddlePanning = false;

    if (gTimelineTool == 1) {
        bool overTimeline = Clay_PointerOver(CLAY_ID("Ruler")) || TimelineTrackUnderPointer() >= 0;
        bool overClip = false;
        for (int i = 0; i < gTimelineClipCount; i++) if (Clay_PointerOver(CLAY_IDI("TimelineClip", i))) { overClip = true; break; }
        if (overTimeline && !overClip && !gTimelineScrubbing && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            gTimelinePanning = true;
            gTimelinePanMouse = mouse.x;
            gTimelinePanMouseY = mouse.y;
            gTimelinePanOrig = gTimelinePan;
            gTimelineVerticalPanOrig = gTimelineVerticalScrollT;
        }
        if (gTimelinePanning && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            gTimelinePan = fminf(0.f, gTimelinePanOrig + mouse.x - gTimelinePanMouse);
            gTimelineVerticalScrollT = gTimelineVerticalPanOrig - (mouse.y - gTimelinePanMouseY);
            gHand = true;
        }
        if (gTimelinePanning && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gTimelinePanning = false;
    }

    int usedAudioTracks = 1;
    for (int i = 0; i < gTimelineClipCount; i++)
        if (gTimelineClips[i].track >= 4)
            usedAudioTracks = usedAudioTracks > gTimelineClips[i].track - 3 ? usedAudioTracks : gTimelineClips[i].track - 3;
    bool draggingTimelineItem = (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) &&
        (gMediaDragIndex >= 0 || gClipDragIndex >= 0);
    if (!draggingTimelineItem) gTimelineAudioTracks = usedAudioTracks;
    else if (gTimelineAudioTracks < usedAudioTracks) gTimelineAudioTracks = usedAudioTracks;

    /* Keep lanes comfortably readable; overflow scrolls instead of squeezing rows. */
    TimelineRebuildVisibleVideoTracks();
    int laneCount = gTimelineVideoTracks + gTimelineAudioTracks;
    gTimelineLaneHeight = fminf(56.f, fmaxf(40.f, (gTimeH - 90.5f) / fmaxf(1, laneCount) - 2.25f));
    float trackViewportHeight = fmaxf(1.f, gTimeH - 90.5f);
    gTimelineVerticalMax = fmaxf(0.f, laneCount * (gTimelineLaneHeight + 2.25f) - trackViewportHeight);
    if (gShiftDown && !gCtrlDown && gWheelDelta != 0.f && inTimelineBody)
        gTimelineVerticalScrollT -= gWheelDelta * fmaxf(48.f, gTimelineLaneHeight * 1.25f);
    gTimelineVerticalScrollT = fmaxf(0.f, fminf(gTimelineVerticalMax, gTimelineVerticalScrollT));
    gTimelineVerticalScroll += (gTimelineVerticalScrollT - gTimelineVerticalScroll) * (1.f - expf(-22.f * gDt));
    if (fabsf(gTimelineVerticalScrollT - gTimelineVerticalScroll) < .25f)
        gTimelineVerticalScroll = gTimelineVerticalScrollT;
    gTimelineVerticalScroll = fmaxf(0.f, fminf(gTimelineVerticalMax, gTimelineVerticalScroll));

    CLAY(CLAY_ID("Timeline"), {.layout = {COLM, .sizing = {GROW, CLAY_SIZING_FIXED(gTimeH)}}, PANEL}) {
        CLAY(CLAY_ID("TlBar"), {.layout = {.sizing = {GROW, FIX(56)}, .padding = {14, 12, 0, 0}, .childGap = 4, .childAlignment = AL_Y}}) {
            if (IconBtn(CLAY_ID("ToolPtr"), I_POINTER, 36, 18, C_TEXT, gTimelineTool == 0, 9)) { gTimelineTool = 0; gTextInputFocused = false; }
            if (IconBtn(CLAY_ID("ToolHand"), I_HAND, 36, 18, C_DIM, gTimelineTool == 1, 9)) { gTimelineTool = 1; gTextInputFocused = false; }
            if (IconBtn(CLAY_ID("ToolCut"), I_SCISSORS, 36, 18, C_DIM, gTimelineTool == 2, 9)) { gTimelineTool = 2; gTextInputFocused = false; }
            if (IconBtn(CLAY_ID("ToolText"), I_TEXT, 36, 18, C_DIM, gTimelineTool == 3, 9)) {
                gTimelineTool = 3;
                gRightTab = 3;
                gTextInputFocused = false;
                gScroll = gScrollT = 0.f;
            }
            GAPX(8)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(1), FIX(20)}}, .backgroundColor = RGB(46, 48, 54)}) {}
            GAPX(8)
            if (IconBtn(CLAY_ID("ToolDel"), I_TRASH, 36, 18, C_DIM, false, 9)) PlutoTimelineDelete(gTimelineSelected);
            GAPX(8)
            if (IconBtn(CLAY_ID("Undo"), I_UNDO, 34, 16, RGB(143, 146, 155), false, 8)) TimelineHistoryAction(false);
            if (IconBtn(CLAY_ID("Redo"), I_REDO, 34, 16, RGB(143, 146, 155), false, 8)) TimelineHistoryAction(true);
            HSPACE
            CLAY(CLAY_ID("KfGroup"), {.layout = {.sizing = {FIT, FIX(32)}, .padding = {2, 2, 0, 0}, .childAlignment = AL_Y},
                                      .backgroundColor = RGB(21, 22, 26), .cornerRadius = CLAY_CORNER_RADIUS(10),
                                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                if (IconBtn(CLAY_ID("KfPrev"), I_KF_PREV, 28, 14, RGB(90, 93, 102), false, 8) &&
                    gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
                    float prev = -1.f;
                    TimelineClipUi *clip = &gTimelineClips[gTimelineSelected];
                    for (int i = 0; i < clip->keyframeCount; i++)
                        if (clip->keyframes[i].time < gPlayhead - .02f && (prev < 0.f || clip->keyframes[i].time > prev)) prev = clip->keyframes[i].time;
                    gPlayhead = prev >= 0.f ? prev : 0.f; gPreviewDirty = true; gPreviewDebounce = 1.f;
                }
                if (IconBtn(CLAY_ID("KfAdd"), I_KF_DIAMOND, 28, 14, C_DIM, false, 8) && gTimelineSelected >= 0)
                    PlutoTimelineAddKeyframe(gPlayhead);
                if (IconBtn(CLAY_ID("KfNext"), I_KF_NEXT, 28, 14, RGB(90, 93, 102), false, 8) &&
                    gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
                    float next = -1.f;
                    TimelineClipUi *clip = &gTimelineClips[gTimelineSelected];
                    for (int i = 0; i < clip->keyframeCount; i++)
                        if (clip->keyframes[i].time > gPlayhead + .02f && (next < 0.f || clip->keyframes[i].time < next)) next = clip->keyframes[i].time;
                    gPlayhead = next >= 0.f ? next : TimelineDuration(); gPreviewDirty = true; gPreviewDebounce = 1.f;
                }
            }
            GAPX(14)
            if (IconBtn(CLAY_ID("ZoomOut"), I_ZOOM_OUT, 28, 16, C_DIM, false, 8)) gTimelineZoom = fmaxf(.35f, gTimelineZoom / 1.15f);
            Clay_ElementId zid = CLAY_ID("ZoomSlider");
            Bs zb = Interact(zid);
            if (Clay_PointerOver(zid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = zid.id;
            if (gDragId == zid.id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Clay_BoundingBox bb = Clay_GetElementData(zid).boundingBox;
                float t = bb.width > 1.f ? (mouse.x - bb.x) / bb.width : 0.f;
                t = fmaxf(0.f, fminf(1.f, t));
                gTimelineZoom = .35f + t * 3.65f;
            }
            CLAY(zid, {.layout = {.sizing = {FIX(125), FIX(20)}}, .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(150, 152, 160), C_CARD, (gTimelineZoom - .35f) / 3.65f, zb.hover * 1.5f, 0)}}) {}
            if (IconBtn(CLAY_ID("ZoomIn"), I_ZOOM_IN, 28, 16, C_DIM, false, 8)) gTimelineZoom = fminf(4.f, gTimelineZoom * 1.15f);
            GAPX(6)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(1), FIX(20)}}, .backgroundColor = RGB(45, 47, 52)}) {}
            GAPX(6)
            Clay_ElementId mid = CLAY_ID("Snap");
            Bs mb = Interact(mid);
            if (mb.clicked) PlutoTimelineSnap(!gTimelineSnap);
            CLAY(mid, {.layout = {.sizing = {FIX(36), FIX(36)}, .childAlignment = AL_C},
                       .backgroundColor = gTimelineSnap ? BtnBg(RGB(27, 29, 34), RGB(36, 38, 44), mb) : BtnBg(RGB(30, 31, 36), RGB(42, 44, 50), mb),
                       .cornerRadius = CLAY_CORNER_RADIUS(10),
                       .border = {.color = gTimelineSnap ? RGB(44, 47, 55) : RGB(48, 50, 58), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                Icon(I_MAGNET, 17, gTimelineSnap ? RGB(157, 163, 176) : C_ICON);
            }
        }
        CLAY(CLAY_ID("TimeBody"), {.layout = {COLM, .sizing = {GROW, GROW}}}) {
            CLAY(CLAY_ID("RulerRow"), {.layout = {.sizing = {GROW, FIX(30)}}}) {
                GAPX(60)
                CLAY(CLAY_ID("Ruler"), {.layout = {.sizing = {GROW, GROW}},
                                         .custom = {.customData = NC(K_RULER, C_CARD, C_CARD, C_CARD, 0,
                                                              fmaxf(0.f, gTimeH - 90.5f), 0)}}) {}
            }
            CLAY(CLAY_ID("TrackViewport"), {.layout = {COLM, .sizing = {GROW, GROW}},
                .clip = {.horizontal = true, .vertical = true, .childOffset = {0, -gTimelineVerticalScroll}}}) {
                for (int i = 0; i < gTimelineVideoTracks; i++)
                    TimelineTrackRow(gTimelineVideoTrackIds[i], gTimelineLaneHeight);
                for (int track = 0; track < gTimelineAudioTracks; track++)
                    TimelineTrackRow(4 + track, gTimelineLaneHeight);
            }
        }
    }
}
