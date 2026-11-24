/* Center panel: preview and transport controls. */
/* ------------------------------------------------------------------ */
/* UI: preview (center)                                                */
/* ------------------------------------------------------------------ */
static const char *Timecode(float seconds) {
    if (seconds < 0.f) seconds = 0.f;
    const int fps = gPreviewFps > 0 ? gPreviewFps : 30;
    int frame = (int)floorf(seconds * (float)fps + .5f);
    int whole = frame / fps, frames = frame % fps;
    return Fmt("%02d:%02d:%02d:%02d", whole / 3600, (whole / 60) % 60, whole % 60, frames);
}

static float PreviewClamp(float value, float low, float high) {
    return fmaxf(low, fminf(high, value));
}

static void UpdateTextToolBox(Clay_ElementId previewFrameId) {
    Clay_BoundingBox bounds = Clay_GetElementData(previewFrameId).boundingBox;
    Rectangle frame = {bounds.x, bounds.y, bounds.width, bounds.height};
    Vector2 mouse = GetMousePosition();
    bool frameValid = frame.width > 1.f && frame.height > 1.f;

    gTextBoxScreenRectValid = false;
    if (gTimelineTool == 3 && gTextTitleSelected && frameValid && !gTextBoxDragging) {
        float width = gTextBoxW * frame.width;
        float height = gTextBoxH * frame.height;
        gTextBoxScreenRect = (Rectangle){
            frame.x + PreviewClamp(gTextPosX - gTextBoxW * .5f, 0.f, 1.f - gTextBoxW) * frame.width,
            frame.y + PreviewClamp(gTextPosY - gTextBoxH * .5f, 0.f, 1.f - gTextBoxH) * frame.height,
            width, height
        };
        gTextBoxScreenRectValid = true;
    }

    bool overFrame = frameValid && CheckCollisionPointRec(mouse, frame);
    if (gTimelineTool == 3 && overFrame) gCross = true;
    if (gTimelineTool == 3 && !gTextBoxDragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && overFrame) {
        static double lastTitleBoxClickAt;
        static char lastTitleBoxClickId[64];
        bool insideSelectedTitle = gTextTitleSelected && gTextBoxScreenRectValid &&
                                   CheckCollisionPointRec(mouse, gTextBoxScreenRect);
        if (insideSelectedTitle) {
            const char *clipId = (gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount)
                ? gTimelineClips[gTimelineSelected].id : "";
            double now = GetTime();
            if (clipId[0] && strcmp(lastTitleBoxClickId, clipId) == 0 &&
                lastTitleBoxClickAt > 0.0 && now - lastTitleBoxClickAt <= .42) {
                /* Double-clicking an existing title enters inline text editing;
                   the caret is drawn over the WGPU preview on the next frame. */
                gTextInputFocused = true;
                gTextFocusStartedAt = now;
                gTextCaretT = now;
                gRightTab = 3;
                lastTitleBoxClickAt = 0.0;
                lastTitleBoxClickId[0] = 0;
            } else {
                lastTitleBoxClickAt = now;
                snprintf(lastTitleBoxClickId, sizeof(lastTitleBoxClickId), "%s", clipId);
            }
        } else {
            lastTitleBoxClickAt = 0.0;
            lastTitleBoxClickId[0] = 0;
            gTextInputFocused = false;
            gTextFocusStartedAt = 0.0;
            gTextBoxDragging = true;
            gTextBoxDragStart = (Vector2){
                PreviewClamp(mouse.x, frame.x, frame.x + frame.width),
                PreviewClamp(mouse.y, frame.y, frame.y + frame.height)
            };
            gTextBoxDragRect = (Rectangle){gTextBoxDragStart.x, gTextBoxDragStart.y, 1.f, 1.f};
            gTextBoxScreenRectValid = false;
        }
    }

    if (gTextBoxDragging) {
        gCross = true;
        float endX = PreviewClamp(mouse.x, frame.x, frame.x + frame.width);
        float endY = PreviewClamp(mouse.y, frame.y, frame.y + frame.height);
        float left = fminf(gTextBoxDragStart.x, endX);
        float top = fminf(gTextBoxDragStart.y, endY);
        gTextBoxDragRect = (Rectangle){left, top,
            fabsf(endX - gTextBoxDragStart.x), fabsf(endY - gTextBoxDragStart.y)};

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            Rectangle selection = gTextBoxDragRect;
            gTextBoxDragging = false;
            if (selection.width >= 12.f && selection.height >= 12.f && frameValid) {
                float x = (selection.x + selection.width * .5f - frame.x) / frame.width;
                float y = (selection.y + selection.height * .5f - frame.y) / frame.height;
                PlutoTextCreateBox(x, y, selection.width / frame.width, selection.height / frame.height);
            }
        }
    }
}

static void CenterPanel(void) {
    static const int resolutions[] = {240, 480, 720, 1080};
    Clay_BoundingBox qb = Clay_GetElementData(CLAY_ID("PreviewQuality")).boundingBox;
    gPreviewQualityRect = (Rectangle){qb.x, qb.y, qb.width, qb.height};
    Clay_BoundingBox fb = Clay_GetElementData(CLAY_ID("PreviewFps")).boundingBox;
    gPreviewFpsRect = (Rectangle){fb.x, fb.y, fb.width, fb.height};
    Rectangle menu = {gPreviewQualityRect.x, gPreviewQualityRect.y - 4 * 32.f - 6.f, 96.f, 4 * 32.f};
    Rectangle fpsMenu = {gPreviewFpsRect.x + gPreviewFpsRect.width - 88.f, gPreviewFpsRect.y - 3 * 32.f - 6.f, 88.f, 3 * 32.f};
    if (gPreviewMenuOpen && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        if (mouse.x >= menu.x && mouse.x <= menu.x + menu.width && mouse.y >= menu.y && mouse.y <= menu.y + menu.height) {
            int option = (int)((mouse.y - menu.y) / 32.f);
            if (option >= 0 && option < 4) { gPreviewRes = resolutions[option]; gPreviewDirty = true; }
            gPreviewMenuOpen = false;
        } else if (!(mouse.x >= gPreviewQualityRect.x && mouse.x <= gPreviewQualityRect.x + gPreviewQualityRect.width &&
                     mouse.y >= gPreviewQualityRect.y && mouse.y <= gPreviewQualityRect.y + gPreviewQualityRect.height)) gPreviewMenuOpen = false;
    }
    if (gPreviewFpsMenuOpen && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        if (mouse.x >= fpsMenu.x && mouse.x <= fpsMenu.x + fpsMenu.width && mouse.y >= fpsMenu.y && mouse.y <= fpsMenu.y + fpsMenu.height) {
            static const int rates[] = {24, 30, 60};
            int option = (int)((mouse.y - fpsMenu.y) / 32.f);
            if (option >= 0 && option < 3) { gPreviewFps = rates[option]; gPreviewDirty = true; }
            gPreviewFpsMenuOpen = false;
        } else if (!(mouse.x >= gPreviewFpsRect.x && mouse.x <= gPreviewFpsRect.x + gPreviewFpsRect.width &&
                     mouse.y >= gPreviewFpsRect.y && mouse.y <= gPreviewFpsRect.y + gPreviewFpsRect.height)) gPreviewFpsMenuOpen = false;
    }
    gPreviewMenuT = EaseIO(AnimLin(CLAY_ID("PreviewMenuAnim").id, gPreviewMenuOpen ? 1.f : 0.f, .16f));
    gPreviewFpsMenuT = EaseIO(AnimLin(CLAY_ID("PreviewFpsMenuAnim").id, gPreviewFpsMenuOpen ? 1.f : 0.f, .16f));
    Clay_BoundingBox pb = Clay_GetElementData(CLAY_ID("PreviewArea")).boundingBox;
    float aw = pb.width - 28, ah = pb.height - 28;
    if (aw < 60 || ah < 60) { aw = 640; ah = 360; }
    float pw, ph;
    if (aw / ah > 16.f / 9.f) { ph = ah; pw = ah * 16.f / 9.f; } else { pw = aw; ph = aw * 9.f / 16.f; }

    CLAY(CLAY_ID("Center"), {.layout = {COLM, .sizing = {GROW, GROW}}, PANEL}) {
        CLAY(CLAY_ID("PreviewArea"), {.layout = {.sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(14), .childAlignment = AL_C},
                                      .clip = {.horizontal = true, .vertical = true}}) {
            CLAY(CLAY_ID("PreviewFrame"), {.layout = {.sizing = {FIX(pw), FIX(ph)}},
                                           .custom = {.customData = NC(K_PREVIEW, C_BG, C_BG, C_BG, 0, 0, 0)}}) {}
        }
        Clay_ElementId previewFrameId = CLAY_ID("PreviewFrame");
        UpdateTextToolBox(previewFrameId);
        CLAY(CLAY_ID("Transport"), {.layout = {.sizing = {GROW, FIX(56)}, .padding = {14, 14, 0, 4}, .childAlignment = AL_Y}}) {
            CLAY(CLAY_ID("TimeL"), {.layout = {.sizing = {GROW, FIT}, .childGap = 7, .childAlignment = AL_Y}}) {
                Tx(Timecode(gPlayhead), 15, C_TEXT, 1);
                Tx("/", 14, C_MUTED, 0);
                Tx(Timecode(TimelineDuration()), 14, C_MUTED, 0);
            }
            CLAY(CLAY_ID("TransC"), {.layout = {.sizing = {FIT, FIT}, .childGap = 12, .childAlignment = AL_Y}}) {
                if (IconBtn(CLAY_ID("SkipB"), I_SKIP_B, 32, 16, C_DIM, false, 16)) { gPlayhead = 0.f; gPreviewDirty = true; }
                if (IconBtn(CLAY_ID("PrevF"), I_CHEV_L, 28, 14, C_DIM, false, 14)) { gPlayhead = fmaxf(0.f, gPlayhead - 1.f / (float)fmaxf(1, gPreviewFps)); gPreviewDirty = true; }
                Clay_ElementId pid = CLAY_ID("Play");
                Bs b = Interact(pid);
                if (b.clicked) gTimelinePlaying = !gTimelinePlaying;
                float sz = 44.f + 3.f * b.hover - 3.f * b.press;
                CLAY(pid, {.layout = {.sizing = {FIX(50), FIX(50)}, .childAlignment = AL_C}}) {
                    CLAY_AUTO_ID({.layout = {.sizing = {FIX(sz), FIX(sz)}, .childAlignment = AL_C},
                                  .backgroundColor = Dark(RGB(244, 244, 247), b.press * .12f), .cornerRadius = CLAY_CORNER_RADIUS(sz / 2)}) {
                        Icon(gTimelinePlaying ? I_PAUSE : I_PLAY, 18, RGB(12, 12, 14));
                    }
                }
                if (IconBtn(CLAY_ID("NextF"), I_CHEV_R, 28, 14, C_DIM, false, 14)) { gPlayhead = fminf(TimelineDuration(), gPlayhead + 1.f / (float)fmaxf(1, gPreviewFps)); gPreviewDirty = true; }
                if (IconBtn(CLAY_ID("SkipF"), I_SKIP_F, 32, 16, C_DIM, false, 16)) { gPlayhead = TimelineDuration(); gPreviewDirty = true; }
            }
            CLAY(CLAY_ID("TransR"), {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}}}) {
                Clay_ElementId previewQuality = CLAY_ID("PreviewQuality");
                Bs q = Interact(previewQuality);
                if (q.clicked) { gPreviewMenuOpen = !gPreviewMenuOpen; gPreviewFpsMenuOpen = false; }
                CLAY(previewQuality, {.layout = {.sizing = {FIT, FIX(30)}, .padding = {10, 10, 0, 0}, .childGap = 5, .childAlignment = AL_Y},
                                      .backgroundColor = Mix(RGB(30, 31, 36), RGB(44, 46, 52), q.hover * .75f),
                                      .cornerRadius = CLAY_CORNER_RADIUS(8),
                                      .border = {.color = Mix(RGB(42, 44, 50), RGB(72, 76, 86), q.hover * .75f), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                    Tx(Fmt("%dp", gPreviewRes), 13, C_TEXT, 0);
                    Icon(I_CHEV_DN, 12, C_MUTED);
                }
                Clay_ElementId previewFps = CLAY_ID("PreviewFps");
                Bs f = Interact(previewFps);
                if (f.clicked) { gPreviewFpsMenuOpen = !gPreviewFpsMenuOpen; gPreviewMenuOpen = false; }
                CLAY(previewFps, {.layout = {.sizing = {FIT, FIX(30)}, .padding = {10, 10, 0, 0}, .childGap = 5, .childAlignment = AL_Y},
                                  .backgroundColor = Mix(RGB(30, 31, 36), RGB(44, 46, 52), f.hover * .75f),
                                  .cornerRadius = CLAY_CORNER_RADIUS(8),
                                  .border = {.color = Mix(RGB(42, 44, 50), RGB(72, 76, 86), f.hover * .75f), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                    Tx(Fmt("%d fps", gPreviewFps), 13, C_TEXT, 0);
                    Icon(I_CHEV_DN, 12, C_MUTED);
                }
                if (IconBtn(CLAY_ID("GridBtn"), I_GRID, 30, 17, C_DIM, gTimelineGrid, 8)) gTimelineGrid = !gTimelineGrid;
            }
        }
    }
}

