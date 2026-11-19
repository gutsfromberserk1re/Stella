/* Center panel: preview and transport controls. */
/* ------------------------------------------------------------------ */
/* UI: preview (center)                                                */
/* ------------------------------------------------------------------ */
static const char *Timecode(float seconds) {
    if (seconds < 0.f) seconds = 0.f;
    int frame = (int)floorf(seconds * 30.f + .5f);
    int whole = frame / 30, frames = frame % 30;
    return Fmt("%02d:%02d:%02d:%02d", whole / 3600, (whole / 60) % 60, whole % 60, frames);
}

static void CenterPanel(void) {
    static const int resolutions[] = {240, 360, 480, 720, 1080};
    Clay_BoundingBox qb = Clay_GetElementData(CLAY_ID("PreviewQuality")).boundingBox;
    gPreviewQualityRect = (Rectangle){qb.x, qb.y, qb.width, qb.height};
    Rectangle menu = {gPreviewQualityRect.x, gPreviewQualityRect.y - 5 * 32.f - 6.f, 96.f, 5 * 32.f};
    if (gPreviewMenuOpen && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        if (mouse.x >= menu.x && mouse.x <= menu.x + menu.width && mouse.y >= menu.y && mouse.y <= menu.y + menu.height) {
            int option = (int)((mouse.y - menu.y) / 32.f);
            if (option >= 0 && option < 5) { gPreviewRes = resolutions[option]; gPreviewDirty = true; }
            gPreviewMenuOpen = false;
        } else if (mouse.x < gPreviewQualityRect.x || mouse.x > gPreviewQualityRect.x + gPreviewQualityRect.width ||
                   mouse.y < menu.y || mouse.y > gPreviewQualityRect.y + gPreviewQualityRect.height) {
            gPreviewMenuOpen = false;
        }
    }
    gPreviewMenuT = EaseIO(AnimLin(CLAY_ID("PreviewMenuAnim").id, gPreviewMenuOpen ? 1.f : 0.f, .16f));
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
        CLAY(CLAY_ID("Transport"), {.layout = {.sizing = {GROW, FIX(56)}, .padding = {14, 14, 0, 4}, .childAlignment = AL_Y}}) {
            CLAY(CLAY_ID("TimeL"), {.layout = {.sizing = {GROW, FIT}, .childGap = 7, .childAlignment = AL_Y}}) {
                Tx(Timecode(gPlayhead), 15, C_TEXT, 1);
                Tx("/", 14, C_MUTED, 0);
                Tx(Timecode(TimelineDuration()), 14, C_MUTED, 0);
            }
            CLAY(CLAY_ID("TransC"), {.layout = {.sizing = {FIT, FIT}, .childGap = 12, .childAlignment = AL_Y}}) {
                if (IconBtn(CLAY_ID("SkipB"), I_SKIP_B, 32, 16, C_DIM, false, 16)) { gPlayhead = 0.f; gPreviewDirty = true; }
                if (IconBtn(CLAY_ID("PrevF"), I_CHEV_L, 28, 14, C_DIM, false, 14)) { gPlayhead = fmaxf(0.f, gPlayhead - 1.f / 30.f); gPreviewDirty = true; }
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
                if (IconBtn(CLAY_ID("NextF"), I_CHEV_R, 28, 14, C_DIM, false, 14)) { gPlayhead = fminf(TimelineDuration(), gPlayhead + 1.f / 30.f); gPreviewDirty = true; }
                if (IconBtn(CLAY_ID("SkipF"), I_SKIP_F, 32, 16, C_DIM, false, 16)) { gPlayhead = TimelineDuration(); gPreviewDirty = true; }
            }
            CLAY(CLAY_ID("TransR"), {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}}}) {
                Clay_ElementId previewQuality = CLAY_ID("PreviewQuality");
                Bs q = Interact(previewQuality);
                if (q.clicked) gPreviewMenuOpen = !gPreviewMenuOpen;
                CLAY(previewQuality, {.layout = {.sizing = {FIT, FIX(30)}, .padding = {10, 10, 0, 0}, .childGap = 5, .childAlignment = AL_Y},
                                      .backgroundColor = Mix(RGB(30, 31, 36), RGB(44, 46, 52), q.hover * .75f),
                                      .cornerRadius = CLAY_CORNER_RADIUS(8),
                                      .border = {.color = Mix(RGB(42, 44, 50), RGB(72, 76, 86), q.hover * .75f), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                    Tx(Fmt("%dp", gPreviewRes), 13, C_TEXT, 0);
                    Icon(I_CHEV_DN, 12, C_MUTED);
                }
                if (IconBtn(CLAY_ID("GridBtn"), I_GRID, 30, 17, C_DIM, gTimelineGrid, 8)) gTimelineGrid = !gTimelineGrid;
            }
        }
    }
}

