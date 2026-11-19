/* Timeline panel. */
/* ------------------------------------------------------------------ */
/* Rust-backed timeline and editing controls                            */
/* ------------------------------------------------------------------ */
static float TimelinePps(void) { return 21.5f * gTimelineZoom; }

static float TimelineDuration(void) {
    float duration = 0.f;
    for (int i = 0; i < gTimelineClipCount; i++)
        duration = fmaxf(duration, gTimelineClips[i].start + gTimelineClips[i].duration);
    return duration > 0.f ? duration : 9.f;
}

static void TimelineClipLane(int track) {
    Clay_ElementId laneId = CLAY_IDI("TrackLane", track);
    Clay_BoundingBox lane = Clay_GetElementData(laneId).boundingBox;
    int order[MAX_TIMELINE_CLIPS], count = 0;
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (gTimelineClips[i].track != track) continue;
        int at = count++;
        while (at > 0 && gTimelineClips[order[at - 1]].start > gTimelineClips[i].start) {
            order[at] = order[at - 1]; at--;
        }
        order[at] = i;
    }

    CLAY(laneId, {.layout = {.sizing = {GROW, FIX(56)}, .childGap = 0}, .clip = {.horizontal = true}}) {
        float cursor = 0.f, pps = TimelinePps();
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
            if (Clay_PointerOver(clipId) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                PlutoTimelineSelect(i);
                if (gTimelineTool == 0) {
                    gClipDragIndex = i;
                    gClipDragTrack = track;
                    gClipDragOrigStart = gTimelineClips[i].start;
                    gClipDragOffset = GetMousePosition().x - bb.x;
                }
            }
            if (gClipDragIndex == i && gTimelineTool == 0) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && lane.width > 0.f) {
                    float start = (GetMousePosition().x - lane.x - gTimelinePan - gClipDragOffset) / pps;
                    gTimelineClips[i].start = fmaxf(0.f, start);
                } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    int targetTrack = track;
                    if (track == 0 && Clay_PointerOver(CLAY_IDI("TrackLane", 1))) targetTrack = 1;
                    else if (track == 1 && Clay_PointerOver(CLAY_IDI("TrackLane", 0))) targetTrack = 0;
                    if (!PlutoTimelineMove(i, gTimelineClips[i].start, targetTrack)) {
                        gTimelineClips[i].start = gClipDragOrigStart;
                        PlutoTimelineCall("{\"command\":\"timeline_get\"}");
                    }
                    gClipDragIndex = -1;
                }
            }
            if (b.clicked && gTimelineTool == 2) PlutoTimelineSplit(i, gPlayhead);
            float cardWidth = fmaxf(1.f, width);
            CLAY(clipId, {.layout = {.sizing = {CLAY_SIZING_FIXED(cardWidth), FIX(48)}, .padding = {8, 5, 2, 0}, .childGap = 6, .childAlignment = AL_Y},
                          .custom = {.customData = NC(K_CLIP, RGB(112, 52, 140), RGB(78, 36, 108), C_CARD, 0, b.hover,
                                                     (i << 1) | (gTimelineSelected == i ? 1 : 0))}}) {
                Icon(gTimelineClips[i].track == 0 ? I_FILM : I_NOTE, 13, C_TEXT);
                if (cardWidth >= 92.f) Tx(gTimelineClips[i].label, 13, C_TEXT, 1);
            }
            cursor = fmaxf(cursor, left + width);
        }
        HSPACE
    }
}

static void Timeline(void) {
    float bodyH = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox.height;
    float extra = fmaxf(0.f, bodyH - 26.f);
    float pps = TimelinePps();
    Vector2 mouse = GetMousePosition();
    if (Clay_PointerOver(CLAY_ID("Ruler")) && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Clay_BoundingBox bb = Clay_GetElementData(CLAY_ID("Ruler")).boundingBox;
        if (bb.width > 0.f) {
            gPlayhead = fmaxf(0.f, (mouse.x - bb.x - gTimelinePan) / pps);
            gPreviewDirty = true;
        }
    }

    if (gMediaDragIndex >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        int targetTrack = Clay_PointerOver(CLAY_IDI("TrackLane", 0)) ? 0 :
                          Clay_PointerOver(CLAY_IDI("TrackLane", 1)) ? 1 : -1;
        if (targetTrack >= 0) {
            Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", targetTrack)).boundingBox;
            float start = lane.width > 0.f ? (mouse.x - lane.x - gTimelinePan) / pps : 0.f;
            PlutoTimelineAddMedia(gMediaDragIndex, fmaxf(0.f, start));
        }
        gMediaDragIndex = -1;
    }

    if (gTimelineTool == 1) {
        bool overTimeline = Clay_PointerOver(CLAY_ID("Ruler")) || Clay_PointerOver(CLAY_IDI("TrackLane", 0)) || Clay_PointerOver(CLAY_IDI("TrackLane", 1));
        if (overTimeline && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            gTimelinePanning = true;
            gTimelinePanMouse = mouse.x;
            gTimelinePanOrig = gTimelinePan;
        }
        if (gTimelinePanning && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) gTimelinePan = gTimelinePanOrig + mouse.x - gTimelinePanMouse;
        if (gTimelinePanning && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gTimelinePanning = false;
    }

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
                    gPlayhead = prev >= 0.f ? prev : 0.f; gPreviewDirty = true;
                }
                if (IconBtn(CLAY_ID("KfAdd"), I_KF_DIAMOND, 28, 14, C_DIM, false, 8) && gTimelineSelected >= 0)
                    PlutoTimelineAddKeyframe(gPlayhead);
                if (IconBtn(CLAY_ID("KfNext"), I_KF_NEXT, 28, 14, RGB(90, 93, 102), false, 8) &&
                    gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
                    float next = -1.f;
                    TimelineClipUi *clip = &gTimelineClips[gTimelineSelected];
                    for (int i = 0; i < clip->keyframeCount; i++)
                        if (clip->keyframes[i] > gPlayhead + .02f && (next < 0.f || clip->keyframes[i] < next)) next = clip->keyframes[i];
                    gPlayhead = next >= 0.f ? next : TimelineDuration(); gPreviewDirty = true;
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
            GAPX(12)
            Clay_ElementId mid = CLAY_ID("Snap");
            Bs mb = Interact(mid);
            if (mb.clicked) PlutoTimelineSnap(!gTimelineSnap);
            CLAY(mid, {.layout = {.sizing = {FIX(36), FIX(36)}, .childAlignment = AL_C},
                       .backgroundColor = gTimelineSnap ? BtnBg(RGB(30, 44, 88), RGB(38, 56, 112), mb) : BtnBg(RGB(30, 31, 36), RGB(46, 48, 54), mb),
                       .cornerRadius = CLAY_CORNER_RADIUS(10),
                       .border = {.color = gTimelineSnap ? RGB(48, 70, 140) : RGB(48, 50, 58), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                Icon(I_MAGNET, 18, gTimelineSnap ? RGB(112, 158, 255) : C_ICON);
            }
        }
        CLAY(CLAY_ID("TimeBody"), {.layout = {COLM, .sizing = {GROW, GROW}}}) {
            CLAY(CLAY_ID("RulerRow"), {.layout = {.sizing = {GROW, FIX(30)}}}) {
                GAPX(60)
                CLAY(CLAY_ID("Ruler"), {.layout = {.sizing = {GROW, GROW}},
                                         .custom = {.customData = NC(K_RULER, C_CARD, C_CARD, C_CARD, 0, extra, 0)}}) {}
            }
            CLAY(CLAY_ID("TrackV1"), {.layout = {.sizing = {GROW, FIX(56)}, .childAlignment = AL_Y}, .backgroundColor = C_ROW,
                                      .border = {.color = RGB(32, 34, 39), .width = {.top = 1, .bottom = 1}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
                    Icon(I_FILM, 14, C_MUTED); Tx("V1", 13, C_MUTED, 0);
                }
                TimelineClipLane(0);
            }
            CLAY(CLAY_ID("TrackA1"), {.layout = {.sizing = {GROW, FIX(56)}, .childAlignment = AL_Y}, .backgroundColor = RGB(21, 22, 26),
                                      .border = {.color = RGB(32, 34, 39), .width = {.bottom = 1}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
                    Icon(I_NOTE, 14, C_MUTED); Tx("A1", 13, C_MUTED, 0);
                }
                TimelineClipLane(1);
            }
        }
    }
}
