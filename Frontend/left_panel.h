/* Left panel: media library and audio controls. */
/* ------------------------------------------------------------------ */
/* UI: library (left)                                                  */
/* ------------------------------------------------------------------ */
static void ValBox(const char *s, const char *unit, float w, float h, float fs, float ufs, float pad);
static void SliderLogic(Clay_ElementId id, int gi, float kr);

static void MediaView(void) {
    CLAY(CLAY_ID("MediaCol"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 9}}) {
        Clay_ElementId sid = CLAY_ID("Search");
        Bs sb = Interact(sid);
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(40)}, .padding = {14, 12, 0, 0}, .childGap = 10, .childAlignment = AL_Y},
                   .backgroundColor = Mix(RGB(30, 31, 36), RGB(36, 37, 43), sb.hover), .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
            Icon(I_SEARCH, 19, Mix(C_MUTED, C_TEXT, sb.hover * .6f));
            Tx("Search media", 16, C_MUTED, 0);
        }
        for (int row = 0; row < 3; row++) {
            CLAY(CLAY_IDI("MRow", row), {.layout = {.sizing = {GROW, FIT}, .childGap = 9}}) {
                for (int col = 0; col < 2; col++) {
                    int i = row * 2 + col;
                    const MediaItem *m = &kMedia[i];
                    CLAY(CLAY_IDI("Media", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
                        Clay_ElementId tid = CLAY_IDI("Thumb", i);
                        Bs b = Interact(tid);
                        CLAY(tid, {.layout = {.sizing = {GROW, FIX(92)}, .padding = {6, 6, 6, 6}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
                                   .custom = {.customData = NC(K_THUMB, m->sky1, m->sky2, m->mtn, 0, b.hover, m->style | (i << 4))}}) {
                            CLAY_AUTO_ID({.layout = {.sizing = {FIT, FIT}, .padding = {5, 5, 1, 2}},
                                          .backgroundColor = RGBA(0, 0, 0, 150), .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
                                Tx(m->dur, 12.5f, C_TEXT, 1);
                            }
                        }
                        Tx(m->name, 14, Mix(C_MUTED, C_DIM, b.hover), 0);
                    }
                }
            }
        }
    }
}

/* value text for knob idx */
static const char *KnobVal(int idx) {
    float v = gKnob[idx];
    if (idx == 0) return Fmt("%d %%", (int)roundf(v * 200.f));
    if (idx == 1) return Fmt("%d", (int)roundf((v - .5f) * 200.f));
    return Fmt("%d dB", (int)roundf((v - .5f) * 24.f));
}

/* draggable knob: drag up/down to change, snaps to the centre detent */
static void KnobGroup(int idx, const char *label) {
    CLAY(CLAY_IDI("KG", idx), {.layout = {COLM, .sizing = {FIT, FIT}, .childGap = 5, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId kid = CLAY_IDI("Knob", idx);
        Bs b = Interact(kid);
        if (Clay_PointerOver(kid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            gDragId = kid.id; gKnobStartV = gKnob[idx]; gKnobStartY = GetMousePosition().y;
        }
        bool drag = false;
        if (gDragId == kid.id) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float v = gKnobStartV + (gKnobStartY - GetMousePosition().y) / 160.f;
                v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
                if (fabsf(v - .5f) < .015f) v = .5f;
                gKnob[idx] = v; drag = true;
            } else gDragId = 0;
        }
        float act = Anim(kid.id + 9u, drag ? 1.f : b.hover * .6f, 20.f);
        float shown = Anim(kid.id + 13u, gKnob[idx], 30.f);      /* smooth pointer motion */
        CLAY(kid, {.layout = {.sizing = {FIX(82), FIX(82)}}, .custom = {.customData = NC(K_KNOB, C_CARD, C_CARD, C_CARD, shown, act, 0)}}) {}
        Tx(label, 14, C_MUTED, 0);
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(74), FIX(28)}, .childAlignment = AL_C},
                      .backgroundColor = RGB(28, 29, 34), .cornerRadius = CLAY_CORNER_RADIUS(7),
                      .border = {.color = Mix(RGB(40, 42, 48), RGB(64, 67, 75), act), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Tx(KnobVal(idx), 14, C_TEXT, 0);
        }
    }
}

static void SwitchItem(int idx, const char *label) {
    Clay_ElementId id = CLAY_IDI("Switch", idx);
    Bs b = Interact(id);
    if (b.clicked) gSwOn[idx] = !gSwOn[idx];
    float on = Anim(id.id + 5u, gSwOn[idx] ? 1.f : 0.f, 22.f);
    CLAY(id, {.layout = {.sizing = {FIT, FIT}, .childGap = 8, .childAlignment = AL_Y}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(46), FIX(26)}},
                      .custom = {.customData = NC(K_SWITCH, C_CARD, C_CARD, C_CARD, on, b.hover * 1.f - b.press * 1.5f, 0)}}) {}
        Tx(label, 14, Mix(C_MUTED, C_DIM, b.hover), 0);
    }
}

static void DurRow(int gi, const char *label) {
    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .padding = {3, 0, 0, 0}, .childGap = 4}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx(label, 13.5f, C_DIM, 0);
            HSPACE
            ValBox(Fmt("%d", 1 + (int)roundf(gSl[gi] * 9.f)), "s", 72, 27, 14.5f, 12.f, 10);
        }
        Clay_ElementId sid = CLAY_IDI("Dur", gi);
        Bs bs = Interact(sid);
        SliderLogic(sid, gi, 7.f);
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(20)}},
                   .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD, gSl[gi],
                                               (bs.hover + (gSlDrag[gi] ? 1.f : 0.f)) * 1.5f, 0)}}) {}
    }
}

/* The whole Audio page (title included) scrolls, like the reference. */
static void AudioView(void) {
    /* scrolling (uses last frame's measured sizes) */
    Clay_BoundingBox vb = Clay_GetElementData(CLAY_ID("AudioScroll")).boundingBox;
    Clay_BoundingBox cb = Clay_GetElementData(CLAY_ID("AudioContent")).boundingBox;
    float maxS = fmaxf(0.f, cb.height - vb.height);
    float wh = GetMouseWheelMove();
    Vector2 mp = GetMousePosition();
    if (wh != 0.f && vb.width > 0.f && mp.x >= vb.x && mp.x <= vb.x + vb.width + 12 && mp.y >= vb.y && mp.y <= vb.y + vb.height)
        gAudT -= wh * 50.f;
    if (gAudT > maxS) gAudT = maxS;
    if (gAudT < 0.f) gAudT = 0.f;
    gAudS += (gAudT - gAudS) * (1.f - expf(-20.f * gDt));
    if (fabsf(gAudT - gAudS) < 0.3f) gAudS = gAudT;
    if (gAudS > maxS) gAudS = maxS;
    float sv = maxS > 1.f ? gAudS / maxS : 0.f;
    float sf = maxS > 1.f && cb.height > 1.f ? vb.height / cb.height : 1.f;

    CLAY(CLAY_ID("AudioBody"), {.layout = {.sizing = {GROW, GROW}, .childGap = 6}}) {
        CLAY(CLAY_ID("AudioScroll"), {.layout = {.sizing = {GROW, GROW}},
                                      .clip = {.vertical = true, .childOffset = {0, -gAudS}}}) {
            CLAY(CLAY_ID("AudioContent"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10, .padding = {0, 0, 0, 12}}}) {
                CLAY(CLAY_ID("AudioHead"), {.layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 0, 6}, .childGap = 10, .childAlignment = AL_Y}}) {
                    Icon(I_NOTE, 22, C_DIM);
                    Tx("Audio", 21.f, C_TEXT, 1);
                }
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { KnobGroup(0, "Volume"); }
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {22, 22, 0, 0}}}) { KnobGroup(1, "Pan"); HSPACE KnobGroup(2, "Bass"); }
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { KnobGroup(3, "Treble"); }
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {0, 0, 14, 4}, .childGap = 40, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
                    SwitchItem(0, "Fade In"); SwitchItem(1, "Fade Out");
                }
                DurRow(40, "Fade In Duration");
                DurRow(41, "Fade Out Duration");
            }
        }
        CLAY(CLAY_ID("AudioBar"), {.layout = {.sizing = {CLAY_SIZING_FIXED(3), GROW}},
                                   .custom = {.customData = NC(K_SCROLLBAR, C_CARD, C_CARD, C_CARD, sv, sf, 0)}}) {}
    }
}

static void LeftPanel(void) {
    /* tab-switch transition (fade + slide), measured from last frame */
    gLTabT = fminf(1.f, gLTabT + gDt / 0.22f);
    gLTabE = EaseIO(gLTabT);
    Clay_BoundingBox lb = Clay_GetElementData(CLAY_ID("LibBody")).boundingBox;
    gLFadeR = (Rectangle){lb.x, lb.y, lb.width, lb.height};
    gLFadeA = (1.f - gLTabE) * 255.f;
    float pill = Anim(CLAY_ID("LibPillAnim").id, (float)gLeftTab, 20.f);

    CLAY(CLAY_ID("Library"), {.layout = {COLM, .sizing = {CLAY_SIZING_FIXED(gLeftW), GROW}, .padding = {13, 13, 12, 12}, .childGap = 12}, PANEL}) {
        CLAY(CLAY_ID("LibHead"), {.layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 0, 0}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_LIBRARY, 22, C_DIM);
            Tx("Library", 21.f, C_TEXT, 1);
            HSPACE
            Clay_ElementId imp = CLAY_ID("Import");
            Bs b = Interact(imp);
            CLAY(imp, {.layout = {.sizing = {FIT, FIX(32)}, .padding = {13, 15, 0, 0}, .childGap = 6, .childAlignment = AL_Y},
                       .backgroundColor = BtnBg(RGB(28, 29, 34), RGB(40, 42, 48), b), .cornerRadius = CLAY_CORNER_RADIUS(9),
                       .border = {.color = Mix(RGB(38, 40, 46), RGB(66, 68, 76), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                Icon(I_PLUS, 14, C_DIM);
                Tx("Import", 15.5f, C_TEXT, 0);
            }
        }
        /* sliding-pill segmented control (shares K_PILL with the right panel, 2 tabs) */
        CLAY(CLAY_ID("LibTabs"), {.layout = {.sizing = {GROW, FIX(40)}, .padding = CLAY_PADDING_ALL(4), .childGap = 4},
                                  .cornerRadius = CLAY_CORNER_RADIUS(13),
                                  .custom = {.customData = NC(K_PILL, C_CARD, C_CARD, C_CARD, pill, 0, 2)}}) {
            for (int i = 0; i < 2; i++) {
                if (TabBtn(CLAY_IDI("LibTab", i), i == 0 ? "Media" : "Audio", gLeftTab == i, 16.5f, 10, true) && gLeftTab != i) {
                    gLeftTab = i; gAudS = gAudT = 0.f; gLTabT = 0.f; gLTabE = 0.f;
                }
            }
        }
        /* NOTE: no vertical clip here. A clipped parent stops Clay from shrinking its GROW
           children, which made the Audio scroll area as tall as its content (nothing to scroll).
           The slide-in is done with top padding instead. */
        CLAY(CLAY_ID("LibBody"), {.layout = {COLM, .sizing = {GROW, GROW},
                                             .padding = {0, 0, (int)roundf((1.f - gLTabE) * 14.f), 0}}}) {
            if (gLeftTab == 0) MediaView(); else AudioView();
        }
    }
}
