/* Timeline panel. */
/* ------------------------------------------------------------------ */
/* UI: timeline                                                        */
/* ------------------------------------------------------------------ */
static void Timeline(void) {
    float bodyH = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox.height;
    float extra = fmaxf(0.f, bodyH - 26.f);

    CLAY(CLAY_ID("Timeline"), {.layout = {COLM, .sizing = {GROW, CLAY_SIZING_FIXED(gTimeH)}}, PANEL}) {
        CLAY(CLAY_ID("TlBar"), {.layout = {.sizing = {GROW, FIX(52)}, .padding = {14, 12, 0, 0}, .childGap = 4, .childAlignment = AL_Y}}) {
            IconBtn(CLAY_ID("ToolPtr"),  I_POINTER,  36, 18, C_TEXT, true, 9);
            IconBtn(CLAY_ID("ToolHand"), I_HAND,     36, 18, C_DIM, false, 9);
            IconBtn(CLAY_ID("ToolCut"),  I_SCISSORS, 36, 18, C_DIM, false, 9);
            IconBtn(CLAY_ID("ToolText"), I_TEXT,     36, 18, C_DIM, false, 9);
            GAPX(8)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(1), FIX(20)}}, .backgroundColor = RGB(46, 48, 54)}) {}
            GAPX(8)
            IconBtn(CLAY_ID("ToolDel"),  I_TRASH,    36, 18, C_DIM, false, 9);
            HSPACE
            CLAY(CLAY_ID("KfGroup"), {.layout = {.sizing = {FIT, FIX(32)}, .padding = {2, 2, 0, 0}, .childAlignment = AL_Y},
                                      .backgroundColor = RGB(21, 22, 26), .cornerRadius = CLAY_CORNER_RADIUS(10),
                                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                IconBtn(CLAY_ID("KfPrev"), I_KF_PREV, 28, 14, RGB(90, 93, 102), false, 8);
                IconBtn(CLAY_ID("KfAdd"),  I_KF_DIAMOND, 28, 14, C_DIM, false, 8);
                IconBtn(CLAY_ID("KfNext"), I_KF_NEXT, 28, 14, RGB(90, 93, 102), false, 8);
            }
            GAPX(14)
            IconBtn(CLAY_ID("ZoomOut"), I_ZOOM_OUT, 28, 16, C_DIM, false, 8);
            Clay_ElementId zid = CLAY_ID("ZoomSlider");
            Bs zb = Interact(zid);
            CLAY(zid, {.layout = {.sizing = {FIX(125), FIX(20)}}, .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(150, 152, 160), C_CARD, .24f, zb.hover * 1.5f, 0)}}) {}
            IconBtn(CLAY_ID("ZoomIn"), I_ZOOM_IN, 28, 16, C_DIM, false, 8);
            GAPX(12)
            Clay_ElementId mid = CLAY_ID("Snap");
            Bs mb = Interact(mid);
            CLAY(mid, {.layout = {.sizing = {FIX(36), FIX(36)}, .childAlignment = AL_C},
                       .backgroundColor = BtnBg(RGB(30, 44, 88), RGB(38, 56, 112), mb), .cornerRadius = CLAY_CORNER_RADIUS(10),
                       .border = {.color = RGB(48, 70, 140), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                Icon(I_MAGNET, 18, RGB(96, 140, 255));
            }
        }
        CLAY(CLAY_ID("TimeBody"), {.layout = {COLM, .sizing = {GROW, GROW}}}) {
            CLAY(CLAY_ID("RulerRow"), {.layout = {.sizing = {GROW, FIX(26)}}}) {
                GAPX(60)
                CLAY(CLAY_ID("Ruler"), {.layout = {.sizing = {GROW, GROW}}, .custom = {.customData = NC(K_RULER, C_CARD, C_CARD, C_CARD, 0, extra, 0)}}) {}
            }
            CLAY(CLAY_ID("TrackV1"), {.layout = {.sizing = {GROW, FIX(47)}, .childAlignment = AL_Y}, .backgroundColor = C_ROW,
                                      .border = {.color = RGB(32, 34, 39), .width = {.top = 1, .bottom = 1}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
                    Icon(I_FILM, 12, C_MUTED); Tx("V1", 11, C_MUTED, 0);
                }
                Clay_ElementId cid = CLAY_ID("Clip");
                Bs cb = Interact(cid);
                CLAY(cid, {.layout = {.sizing = {FIX(193), FIX(42)}, .padding = {10, 6, 5, 0}, .childGap = 5},
                           .custom = {.customData = NC(K_CLIP, RGB(112, 52, 140), RGB(78, 36, 108), C_CARD, 0, cb.hover, 0)}}) {
                    Icon(I_FILM, 11, C_TEXT);
                    Tx("City at dusk.mp4", 11, C_TEXT, 1);
                }
            }
            CLAY(CLAY_ID("TrackA1"), {.layout = {.sizing = {GROW, FIX(47)}, .childAlignment = AL_Y}, .backgroundColor = RGB(21, 22, 26),
                                      .border = {.color = RGB(32, 34, 39), .width = {.bottom = 1}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
                    Icon(I_NOTE, 12, C_MUTED); Tx("A1", 11, C_MUTED, 0);
                }
            }
        }
    }
}

