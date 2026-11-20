/* Timeline panel. */
/* ------------------------------------------------------------------ */
/* Rust-backed timeline and editing controls                            */
/* ------------------------------------------------------------------ */
static float TimelinePps(void) { return 21.5f * gTimelineZoom; }
static float gTimelineLaneHeight = 56.f;

static int TimelineTrackUnderPointer(void) {
    for (int track = 0; track < gTimelineVideoTracks; track++)
        if (Clay_PointerOver(CLAY_IDI("TrackLane", track))) return track;
    for (int track = 4; track < 4 + gTimelineAudioTracks; track++)
        if (Clay_PointerOver(CLAY_IDI("TrackLane", track))) return track;
    return -1;
}

static bool TimelineTrackIsVideo(int track) { return track >= 0 && track < 4; }

static float TimelineSnapStart(float start, float duration, bool video, int movingIndex) {
    if (!gTimelineSnap || !video) return fmaxf(0.f, start);
    float best = .25f, snapped = start;
    for (int n = -1; n < gTimelineClipCount; n++) {
        if (n == movingIndex) continue;
        if (n >= 0 && !TimelineTrackIsVideo(gTimelineClips[n].track)) continue;
        float edges[2] = {n < 0 ? 0.f : gTimelineClips[n].start,
                          n < 0 ? 0.f : gTimelineClips[n].start + gTimelineClips[n].duration};
        for (int edge = 0; edge < 2; edge++) {
            float candidates[2] = {start, start + duration};
            for (int c = 0; c < 2; c++) {
                float distance = fabsf(edges[edge] - candidates[c]);
                if (distance <= best) { best = distance; snapped = edges[edge] - (c ? duration : 0.f); }
            }
        }
    }
    return fmaxf(0.f, snapped);
}

static bool TimelineTrackOverlaps(int track, float start, float duration, int excluded) {
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (i == excluded || gTimelineClips[i].track != track) continue;
        if (start < gTimelineClips[i].start + gTimelineClips[i].duration - .0001f &&
            start + duration > gTimelineClips[i].start + .0001f) return true;
    }
    return false;
}

static int TimelineClosestFreeTrack(int requested, float start, float duration, int excluded) {
    int first = TimelineTrackIsVideo(requested) ? 0 : 4;
    int last = TimelineTrackIsVideo(requested) ? 3 : 5;
    int best = requested, bestDistance = 99;
    for (int track = first; track <= last; track++) {
        int distance = abs(track - requested);
        if (distance < bestDistance && !TimelineTrackOverlaps(track, start, duration, excluded)) {
            best = track; bestDistance = distance;
        }
    }
    return best;
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

static void TimelineClipLane(int track) {
    Clay_ElementId laneId = CLAY_IDI("TrackLane", track);
    Clay_BoundingBox lane = Clay_GetElementData(laneId).boundingBox;
    float pps = TimelinePps();
    if (gTimelineTool == 2 && Clay_PointerOver(laneId)) gCross = true;
    bool transitionPressed = false;
    int order[MAX_TIMELINE_CLIPS], count = 0;
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (gTimelineClips[i].track != track) continue;
        int at = count++;
        while (at > 0 && gTimelineClips[order[at - 1]].start > gTimelineClips[i].start) {
            order[at] = order[at - 1]; at--;
        }
        order[at] = i;
    }

    if (track < 4) {
        for (int n = 0; n + 1 < count && gTransitionButtonCount < 64; n++) {
            int left = order[n], right = order[n + 1];
            float end = gTimelineClips[left].start + gTimelineClips[left].duration;
            if (fabsf(end - gTimelineClips[right].start) > .04f) continue;
            float leftEdge = lane.x + gTimelinePan + gTimelineClips[left].start * TimelinePps();
            float rightEdge = lane.x + gTimelinePan + gTimelineClips[right].start * TimelinePps();
            float leftVisibleEnd = leftEdge + fmaxf(46.f, gTimelineClips[left].duration * TimelinePps());
            float joinX = fmaxf(rightEdge, leftVisibleEnd);
            Rectangle rect = {joinX - 11.f, lane.y + (lane.height - 22.f) * .5f, 22.f, 22.f};
            gTransitionButtons[gTransitionButtonCount++] = (TransitionButton){rect, left};
            if (CheckCollisionPointRec(GetMousePosition(), rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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
                start = TimelineSnapStart(start, media->duration, !mediaIsAudio, -1);
                int target = TimelineClosestFreeTrack(track, start, media->duration, -1);
                if (target < 4 && gTimelineVideoTracks < target + 1) gTimelineVideoTracks = target + 1;
                if (target >= 4 && gTimelineAudioTracks < target - 3) gTimelineAudioTracks = target - 3;
                Clay_BoundingBox targetBox = Clay_GetElementData(CLAY_IDI("TrackLane", target)).boundingBox;
                float x = targetBox.x + gTimelinePan + start * pps;
                float w = fmaxf(46.f, media->duration * pps);
                float left = fmaxf(targetBox.x, x), right = fminf(targetBox.x + targetBox.width, x + w);
                if (right > left) {
                    gMediaDropPreview = (Rectangle){left, targetBox.y + (targetBox.height - 38.f) * .5f, right - left, 38.f};
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
            if (!transitionPressed && !gTimelineScrubbing && !gTimelineContextConsumedClick &&
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
                }
            }
            if (Clay_PointerOver(clipId) && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                if (!gTimelineMultiSelect[i]) {
                    memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                    gTimelineMultiSelect[i] = true;
                }
                gTimelineSelected = i;
                PlutoTimelineSelect(i);
                gTimelineContextClip = i;
                gTimelineContextPos = GetMousePosition();
                gTimelineContextOpen = true;
            }
            if (gClipDragIndex == i && (gTimelineTool == 0 || gTimelineTool == 1)) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && lane.width > 0.f) {
                    int hoveredTrack = TimelineTrackUnderPointer();
                    int requestedTrack = hoveredTrack >= 0 && TimelineTrackIsVideo(hoveredTrack) == TimelineTrackIsVideo(gClipDragTrack)
                        ? hoveredTrack : gClipDragTrack;
                    Clay_BoundingBox targetLane = Clay_GetElementData(CLAY_IDI("TrackLane", requestedTrack)).boundingBox;
                    float start = fmaxf(0.f, (GetMousePosition().x - targetLane.x - gTimelinePan - gClipDragOffset) / pps);
                    start = TimelineSnapStart(start, gTimelineClips[i].duration, TimelineTrackIsVideo(gClipDragTrack), i);
                    int targetTrack = TimelineClosestFreeTrack(requestedTrack, start, gTimelineClips[i].duration, i);
                    if (targetTrack < 4 && gTimelineVideoTracks < targetTrack + 1) gTimelineVideoTracks = targetTrack + 1;
                    if (targetTrack >= 4 && gTimelineAudioTracks < targetTrack - 3) gTimelineAudioTracks = targetTrack - 3;
                    gTimelineClips[i].start = start;
                    Clay_BoundingBox targetBox = Clay_GetElementData(CLAY_IDI("TrackLane", targetTrack)).boundingBox;
                    float x = targetBox.x + gTimelinePan + start * pps;
                    float w = fmaxf(46.f, gTimelineClips[i].duration * pps);
                    float left = fmaxf(targetBox.x, x), right = fminf(targetBox.x + targetBox.width, x + w);
                    if (right > left) {
                        gMediaDropPreview = (Rectangle){left, targetBox.y + (targetBox.height - 38.f) * .5f, right - left, 38.f};
                        gMediaDropPreviewVisible = true;
                        gHand = true;
                    }
                } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    int hoveredTrack = TimelineTrackUnderPointer();
                    int requestedTrack = hoveredTrack >= 0 && TimelineTrackIsVideo(hoveredTrack) == TimelineTrackIsVideo(gClipDragTrack)
                        ? hoveredTrack : gClipDragTrack;
                    Clay_BoundingBox targetLane = Clay_GetElementData(CLAY_IDI("TrackLane", requestedTrack)).boundingBox;
                    float start = targetLane.width > 0.f
                        ? fmaxf(0.f, (GetMousePosition().x - targetLane.x - gTimelinePan - gClipDragOffset) / pps)
                        : gTimelineClips[i].start;
                    start = TimelineSnapStart(start, gTimelineClips[i].duration, TimelineTrackIsVideo(gClipDragTrack), i);
                    int targetTrack = TimelineClosestFreeTrack(requestedTrack, start, gTimelineClips[i].duration, i);
                    if (!PlutoTimelineMove(i, start, targetTrack)) {
                        gTimelineClips[i].start = gClipDragOrigStart;
                        gTimelineClips[i].track = gClipDragTrack;
                        PlutoTimelineCall("{\"command\":\"timeline_get\"}");
                    }
                    gClipDragIndex = -1;
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
            CLAY(clipId, {.layout = {.sizing = {CLAY_SIZING_FIXED(cardWidth), FIX(fminf(48.f, lane.height - 6.f))}, .padding = {8, 5, 2, 0}, .childGap = 6, .childAlignment = AL_Y},
                          .custom = {.customData = NC(K_CLIP, RGB(112, 52, 140), RGB(78, 36, 108), C_CARD, 0, b.hover,
                                                     (i << 1) | (clipSelected ? 1 : 0))}}) {
                Icon(TimelineTrackIsVideo(gTimelineClips[i].track) ? I_FILM : I_NOTE, 13, C_TEXT);
                if (cardWidth >= 92.f) Tx(gTimelineClips[i].label, 13, C_TEXT, 1);
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
    float bodyH = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox.height;
    float extra = fmaxf(0.f, bodyH - 26.f);
    float pps = TimelinePps();
    Vector2 mouse = GetMousePosition();
    Clay_BoundingBox ruler = Clay_GetElementData(CLAY_ID("Ruler")).boundingBox;
    Clay_BoundingBox body = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox;
    float playheadX = ruler.x + gTimelinePan + gPlayhead * pps;
    bool grabbedPlayhead = mouse.y >= ruler.y && mouse.y <= body.y + body.height && fabsf(mouse.x - playheadX) <= 9.f;
    if ((Clay_PointerOver(CLAY_ID("Ruler")) || grabbedPlayhead) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gTimelineScrubbing = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gTimelineScrubbing = false;
    if (gTimelineScrubbing) {
        Clay_BoundingBox bb = Clay_GetElementData(CLAY_ID("Ruler")).boundingBox;
        if (bb.width > 0.f) {
            float seconds = fmaxf(0.f, (mouse.x - bb.x - gTimelinePan) / pps);
            gPlayhead = floorf(seconds * 30.f + .5f) / 30.f;
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
                start = TimelineSnapStart(start, media->duration, !audioTrack, -1);
                targetTrack = TimelineClosestFreeTrack(targetTrack, start, media->duration, -1);
                PlutoTimelineAddMedia(gMediaDragIndex, start, targetTrack);
            }
        }
        gMediaDragIndex = -1;
    }

    if (gTimelineTool == 1) {
        bool overTimeline = Clay_PointerOver(CLAY_ID("Ruler")) || TimelineTrackUnderPointer() >= 0;
        bool overClip = false;
        for (int i = 0; i < gTimelineClipCount; i++) if (Clay_PointerOver(CLAY_IDI("TimelineClip", i))) { overClip = true; break; }
        if (overTimeline && !overClip && !gTimelineScrubbing && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            gTimelinePanning = true;
            gTimelinePanMouse = mouse.x;
            gTimelinePanOrig = gTimelinePan;
        }
        if (gTimelinePanning && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) gTimelinePan = gTimelinePanOrig + mouse.x - gTimelinePanMouse;
        if (gTimelinePanning && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gTimelinePanning = false;
    }

    int usedVideoTracks = 1, usedAudioTracks = 1;
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (gTimelineClips[i].track < 4) usedVideoTracks = usedVideoTracks > gTimelineClips[i].track + 1 ? usedVideoTracks : gTimelineClips[i].track + 1;
        else usedAudioTracks = usedAudioTracks > gTimelineClips[i].track - 3 ? usedAudioTracks : gTimelineClips[i].track - 3;
    }
    bool draggingTimelineItem = (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) &&
        (gMediaDragIndex >= 0 || gClipDragIndex >= 0);
    if (!draggingTimelineItem) {
        gTimelineVideoTracks = usedVideoTracks;
        gTimelineAudioTracks = usedAudioTracks;
    } else {
        if (gTimelineVideoTracks < usedVideoTracks) gTimelineVideoTracks = usedVideoTracks;
        if (gTimelineAudioTracks < usedAudioTracks) gTimelineAudioTracks = usedAudioTracks;
        Clay_BoundingBox topVideo = Clay_GetElementData(CLAY_IDI("TrackLane", gTimelineVideoTracks - 1)).boundingBox;
        if (topVideo.width > 0.f && mouse.y < topVideo.y && gTimelineVideoTracks < 4) gTimelineVideoTracks++;
        Clay_BoundingBox bottomAudio = Clay_GetElementData(CLAY_IDI("TrackLane", 3 + gTimelineAudioTracks)).boundingBox;
        if (bottomAudio.width > 0.f && mouse.y > bottomAudio.y + bottomAudio.height && gTimelineAudioTracks < 2) gTimelineAudioTracks++;
    }
    int laneCount = gTimelineVideoTracks + gTimelineAudioTracks;
    gTimeH = fminf(TL_MAX, fmaxf(gTimeH, 86.f + laneCount * 40.f));
    gTimelineLaneHeight = fminf(56.f, fmaxf(24.f, (gTimeH - 86.f) / fmaxf(1, laneCount)));

    CLAY(CLAY_ID("Timeline"), {.layout = {COLM, .sizing = {GROW, CLAY_SIZING_FIXED(gTimeH)}}, PANEL}) {
        CLAY(CLAY_ID("TlBar"), {.layout = {.sizing = {GROW, FIX(56)}, .padding = {14, 12, 0, 0}, .childGap = 4, .childAlignment = AL_Y}}) {
            if (IconBtn(CLAY_ID("ToolPtr"), I_POINTER, 36, 18, C_TEXT, gTimelineTool == 0, 9)) gTimelineTool = 0;
            if (IconBtn(CLAY_ID("ToolHand"), I_HAND, 36, 18, C_DIM, gTimelineTool == 1, 9)) gTimelineTool = 1;
            if (IconBtn(CLAY_ID("ToolCut"), I_SCISSORS, 36, 18, C_DIM, gTimelineTool == 2, 9)) gTimelineTool = 2;
            if (IconBtn(CLAY_ID("ToolText"), I_TEXT, 36, 18, C_DIM, gTimelineTool == 3, 9)) gTimelineTool = 3;
            GAPX(8)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(1), FIX(20)}}, .backgroundColor = RGB(46, 48, 54)}) {}
            GAPX(8)
            if (IconBtn(CLAY_ID("ToolDel"), I_TRASH, 36, 18, C_DIM, false, 9)) PlutoTimelineDelete(gTimelineSelected);
            HSPACE
            CLAY(CLAY_ID("KfGroup"), {.layout = {.sizing = {FIT, FIX(32)}, .padding = {2, 2, 0, 0}, .childAlignment = AL_Y},
                                      .backgroundColor = RGB(21, 22, 26), .cornerRadius = CLAY_CORNER_RADIUS(10),
                                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                if (IconBtn(CLAY_ID("KfPrev"), I_KF_PREV, 28, 14, RGB(90, 93, 102), false, 8) &&
                    gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
                    float prev = -1.f;
                    TimelineClipUi *clip = &gTimelineClips[gTimelineSelected];
                    for (int i = 0; i < clip->keyframeCount; i++)
                        if (clip->keyframes[i] < gPlayhead - .02f && (prev < 0.f || clip->keyframes[i] > prev)) prev = clip->keyframes[i];
                    gPlayhead = prev >= 0.f ? prev : 0.f; gPreviewDirty = true; gPreviewDebounce = 1.f;
                }
                if (IconBtn(CLAY_ID("KfAdd"), I_KF_DIAMOND, 28, 14, C_DIM, false, 8) && gTimelineSelected >= 0)
                    PlutoTimelineAddKeyframe(gPlayhead);
                if (IconBtn(CLAY_ID("KfNext"), I_KF_NEXT, 28, 14, RGB(90, 93, 102), false, 8) &&
                    gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
                    float next = -1.f;
                    TimelineClipUi *clip = &gTimelineClips[gTimelineSelected];
                    for (int i = 0; i < clip->keyframeCount; i++)
                        if (clip->keyframes[i] > gPlayhead + .02f && (next < 0.f || clip->keyframes[i] < next)) next = clip->keyframes[i];
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
                                         .custom = {.customData = NC(K_RULER, C_CARD, C_CARD, C_CARD, 0, extra, 0)}}) {}
            }
            for (int track = gTimelineVideoTracks - 1; track >= 0; track--) TimelineTrackRow(track, gTimelineLaneHeight);
            for (int track = 0; track < gTimelineAudioTracks; track++) TimelineTrackRow(4 + track, gTimelineLaneHeight);
        }
    }
}
