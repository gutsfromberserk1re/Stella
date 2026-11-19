/* Center panel: preview and transport controls. */
/* ------------------------------------------------------------------ */
/* UI: preview (center)                                                */
/* ------------------------------------------------------------------ */
static void CenterPanel(void) {
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
                Tx("00:00:00:00", 15, C_TEXT, 1);
                Tx("/", 14, C_MUTED, 0);
                Tx("00:00:09:00", 14, C_MUTED, 0);
            }
            CLAY(CLAY_ID("TransC"), {.layout = {.sizing = {FIT, FIT}, .childGap = 12, .childAlignment = AL_Y}}) {
                IconBtn(CLAY_ID("SkipB"), I_SKIP_B, 32, 16, C_DIM, false, 16);
                IconBtn(CLAY_ID("PrevF"), I_CHEV_L, 28, 14, C_DIM, false, 14);
                Clay_ElementId pid = CLAY_ID("Play");
                Bs b = Interact(pid);
                float sz = 44.f + 3.f * b.hover - 3.f * b.press;
                CLAY(pid, {.layout = {.sizing = {FIX(50), FIX(50)}, .childAlignment = AL_C}}) {
                    CLAY_AUTO_ID({.layout = {.sizing = {FIX(sz), FIX(sz)}, .childAlignment = AL_C},
                                  .backgroundColor = Dark(RGB(244, 244, 247), b.press * .12f), .cornerRadius = CLAY_CORNER_RADIUS(sz / 2)}) {
                        Icon(I_PLAY, 18, RGB(12, 12, 14));
                    }
                }
                IconBtn(CLAY_ID("NextF"), I_CHEV_R, 28, 14, C_DIM, false, 14);
                IconBtn(CLAY_ID("SkipF"), I_SKIP_F, 32, 16, C_DIM, false, 16);
            }
            CLAY(CLAY_ID("TransR"), {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}}}) {
                Clay_ElementId previewQuality = CLAY_ID("PreviewQuality");
                Bs q = Interact(previewQuality);
                if (q.clicked) {
                    static const int levels[] = {240, 360, 480, 720, 1080};
                    int i = 0;
                    while (i < 4 && levels[i] != gPreviewRes) i++;
                    gPreviewRes = levels[(i + 1) % 5];
                }
                CLAY(previewQuality, {.layout = {.sizing = {FIT, FIX(30)}, .padding = {10, 10, 0, 0}, .childGap = 5, .childAlignment = AL_Y},
                                      .backgroundColor = Mix(RGB(30, 31, 36), RGB(44, 46, 52), q.hover * .75f),
                                      .cornerRadius = CLAY_CORNER_RADIUS(8),
                                      .border = {.color = Mix(RGB(42, 44, 50), RGB(72, 76, 86), q.hover * .75f), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                    Tx(Fmt("Preview %dp", gPreviewRes), 12, C_TEXT, 0);
                    Icon(I_CHEV_DN, 12, C_MUTED);
                }
                IconBtn(CLAY_ID("Loop"), I_LOOP, 30, 17, C_DIM, false, 8);
                IconBtn(CLAY_ID("GridBtn"), I_GRID, 30, 17, C_DIM, false, 8);
            }
        }
    }
}

