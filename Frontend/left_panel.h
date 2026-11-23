/* Left panel: media library and audio controls. */
/* ------------------------------------------------------------------ */
/* UI: library (left)                                                  */
/* ------------------------------------------------------------------ */
static bool ValBoxEditable(Clay_ElementId id, float *value, const char *unit,
                           float w, float h, float fs, float ufs, float pad,
                           float minimum, float maximum, int decimals);
static bool ValBoxTyped(Clay_ElementId id, float *value, const char *unit,
                        float w, float h, float fs, float ufs, float pad,
                        float minimum, float maximum, int decimals);
static void SliderLogic(Clay_ElementId id, int gi, float kr);

static void MediaView(void) {
    gMediaHoverName[0] = 0;
    Clay_BoundingBox mediaBox = Clay_GetElementData(CLAY_ID("LibBody")).boundingBox;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    Vector2 mouse = GetMousePosition();
    float wheel = gWheelDelta;
    if (ctrl && wheel != 0.f && mouse.x >= mediaBox.x && mouse.x <= mediaBox.x + mediaBox.width &&
        mouse.y >= mediaBox.y && mouse.y <= mediaBox.y + mediaBox.height) {
        gMediaZoom += wheel > 0.f ? .1f : -.1f;
        if (gMediaZoom < .65f) gMediaZoom = .65f;
        if (gMediaZoom > 2.2f) gMediaZoom = 2.2f;
    }
    if (gLeftTab == 0) {
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) gMediaZoom = fminf(2.2f, gMediaZoom + .1f);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) gMediaZoom = fmaxf(.65f, gMediaZoom - .1f);
    }
    float desiredTileWidth = 100.f * gMediaZoom;
    int columns = (int)floorf((mediaBox.width - 18.f + 9.f) / (desiredTileWidth + 9.f));
    if (columns < 1) columns = 1;
    if (columns > 3) columns = 3;
    float tileWidth = fmaxf(72.f, fminf(desiredTileWidth,
        (mediaBox.width - 18.f - 9.f * (columns - 1)) / columns));
    int mediaCount = gImportedCount;
    int rows = (mediaCount + columns - 1) / columns;
    CLAY(CLAY_ID("LibContent"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 9, .padding = {0, 0, 0, 12}}}) {
        Clay_ElementId sid = CLAY_ID("Search");
        Bs sb = Interact(sid);
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(40)}, .padding = {14, 12, 0, 0}, .childGap = 10, .childAlignment = AL_Y},
                   .backgroundColor = Mix(RGB(30, 31, 36), RGB(36, 37, 43), sb.hover), .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
            Icon(I_SEARCH, 19, Mix(C_MUTED, C_TEXT, sb.hover * .6f));
            Tx("Search media", 16, C_MUTED, 0);
        }
        for (int row = 0; row < rows; row++) {
            CLAY(CLAY_IDI("MRow", row), {.layout = {.sizing = {GROW, FIT}, .childGap = 9}}) {
                for (int col = 0; col < columns; col++) {
                    int i = row * columns + col;
                    if (i >= mediaCount) break;
                    ImportedMedia *m = &gImported[i];
                    const char *name = m->name;
                    const char *duration = Fmt("%d:%02d", (int)m->duration / 60, (int)m->duration % 60);
                    CLAY(CLAY_IDI("Media", i), {.layout = {COLM, .sizing = {FIX(tileWidth), FIT}, .childGap = 6}}) {
                        Clay_ElementId tid = CLAY_IDI("Thumb", i);
                        Bs b = Interact(tid);
                        Clay_BoundingBox tb = Clay_GetElementData(tid).boundingBox;
                        if (Clay_PointerOver(tid)) {
                            strncpy(gMediaHoverName, name, sizeof(gMediaHoverName) - 1);
                            gMediaHoverName[sizeof(gMediaHoverName) - 1] = 0;
                            gMediaHoverRect = (Rectangle){tb.x, tb.y, tb.width, tb.height};
                        }
                        if (Clay_PointerOver(tid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gMediaDragIndex = i;
                        if (b.clicked) gMediaSelected = i;
                        CLAY(tid, {.layout = {.sizing = {GROW, FIX(tileWidth * 9.f / 16.f)}, .padding = {6, 6, 6, 6}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
                                   .border = {.color = gMediaSelected == i ? C_TEXT : RGBA(0, 0, 0, 0), .width = CLAY_BORDER_OUTSIDE(gMediaSelected == i ? 2 : 0)},
                                   .custom = {.customData = NC(K_THUMB, C_CARD, C_CARD, C_CARD, 0, b.hover, 0x1000 | (i << 4))}}) {
                            CLAY_AUTO_ID({.layout = {.sizing = {FIT, FIT}, .padding = {5, 5, 1, 2}},
                                          .backgroundColor = RGBA(0, 0, 0, 150), .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
                                Tx(duration, 12.5f, C_TEXT, 1);
                            }
                        }
                    }
                }
            }
        }
    }
}

/* value text for knob idx */
/* draggable knob: drag up/down to change, snaps to the centre detent */
static void KnobGroup(int idx, const char *label) {
    static float sent[4] = {.5f, .5f, .5f, .5f};
    static double lastClickTime[4];
    static bool clickMoved[4];
    static const char *paths[4] = {"props.audio.volume", "props.audio.pan", "props.audio.bass", "props.audio.treble"};
    CLAY(CLAY_IDI("KG", idx), {.layout = {COLM, .sizing = {FIT, FIT}, .childGap = 5, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId kid = CLAY_IDI("Knob", idx);
        Bs b = Interact(kid);
        if (Clay_PointerOver(kid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            double now = GetTime();
            if (lastClickTime[idx] > 0.0 && now - lastClickTime[idx] < 0.38 && !clickMoved[idx]) {
                gKnob[idx] = .5f;
                gDragId = 0;
                lastClickTime[idx] = 0.0;
            } else {
                gDragId = kid.id;
                gKnobStartV = gKnob[idx];
                gKnobStartY = GetMousePosition().y;
                clickMoved[idx] = false;
                lastClickTime[idx] = now;
            }
        }
        bool drag = false;
        if (gDragId == kid.id) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float mouseY = GetMousePosition().y;
                if (fabsf(mouseY - gKnobStartY) > 2.5f) clickMoved[idx] = true;
                float v = gKnobStartV + (gKnobStartY - mouseY) / 160.f;
                v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
                if (fabsf(v - .5f) < .015f) v = .5f;
                gKnob[idx] = v; drag = true;
            } else gDragId = 0;
        }
        if (!drag && fabsf(gKnob[idx] - sent[idx]) > .002f) {
            double value = idx == 0 ? gKnob[idx] * 200.0 : idx == 1 ? (gKnob[idx] - .5) * 200.0 : (gKnob[idx] - .5) * 24.0;
            PlutoTimelineSetProperty(paths[idx], value);
            sent[idx] = gKnob[idx];
        }
        float act = Anim(kid.id + 9u, drag ? 1.f : b.hover * .6f, 20.f);
        float shown = Anim(kid.id + 13u, gKnob[idx], 30.f);      /* smooth pointer motion */
        static float sPos[4] = {1.f, 1.f, 1.f, 1.f}, sVel[4] = {0.f, 0.f, 0.f, 0.f};
        float target = drag ? .93f : 1.f, dtc = gDt > 1.f / 30.f ? 1.f / 30.f : gDt;
        const float k = 380.f, damp = 2.f * .42f * sqrtf(k);
        for (float t = 0.f; t < dtc; t += 1.f / 240.f) {
            float h = fminf(1.f / 240.f, dtc - t);
            sVel[idx] += (-k * (sPos[idx] - target) - damp * sVel[idx]) * h;
            sPos[idx] += sVel[idx] * h;
        }
        CLAY(kid, {.layout = {.sizing = {FIX(72), FIX(72)}},
                   .custom = {.customData = NC(K_KNOB, C_CARD, ((Clay_Color){sPos[idx], 0, 0, 255}), C_CARD, shown, act, 0)}}) {}
        Tx(label, 14, C_MUTED, 0);
        float shownValue = idx == 0 ? roundf(gKnob[idx] * 200.f) :
                           idx == 1 ? roundf((gKnob[idx] - .5f) * 200.f) : roundf((gKnob[idx] - .5f) * 24.f);
        const char *unit = idx == 0 ? "%" : idx >= 2 ? "dB" : NULL;
        float minimum = idx == 0 ? 0.f : idx == 1 ? -100.f : -12.f;
        float maximum = idx == 0 ? 200.f : idx == 1 ? 100.f : 12.f;
        if (ValBoxTyped(CLAY_IDI("KnobValue", idx), &shownValue, unit, 74, 28, 15.5f, 12.5f, 9,
                        minimum, maximum, 0)) {
            gKnob[idx] = idx == 0 ? shownValue / 200.f : idx == 1 ? shownValue / 200.f + .5f : shownValue / 24.f + .5f;
            double value = shownValue;
            PlutoTimelineSetProperty(paths[idx], value);
            sent[idx] = gKnob[idx];
        }
    }
}

static void SwitchItem(int idx, const char *label) {
    Clay_ElementId id = CLAY_IDI("Switch", idx);
    Bs b = Interact(id);
    if (b.clicked) {
        gSwOn[idx] = !gSwOn[idx];
        PlutoTimelineSetBoolProperty(idx == 0 ? "props.audio.fadeIn" : "props.audio.fadeOut", gSwOn[idx]);
    }
    float on = Anim(id.id + 5u, gSwOn[idx] ? 1.f : 0.f, 22.f);
    CLAY(id, {.layout = {.sizing = {FIT, FIT}, .childGap = 8, .childAlignment = AL_Y}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(46), FIX(26)}},
                      .custom = {.customData = NC(K_SWITCH, C_CARD, C_CARD, C_CARD, on, b.hover * 1.f - b.press * 1.5f, 0)}}) {}
        Tx(label, 14, Mix(C_MUTED, C_DIM, b.hover), 0);
    }
}

static void DurRow(int gi, const char *label) {
    static float sent[2] = {0.f, 0.f};
    int slot = gi == 40 ? 0 : 1;
    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .padding = {3, 0, 0, 0}, .childGap = 4}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx(label, 13.5f, C_DIM, 0);
            HSPACE
            float durationValue = 1.f + roundf(gSl[gi] * 9.f);
            if (ValBoxEditable(CLAY_IDI("DurationValue", gi), &durationValue, "s", 72, 27, 14.5f, 12.f, 10, 1.f, 10.f, 0)) {
                gSl[gi] = gSlT[gi] = (durationValue - 1.f) / 9.f;
                PlutoTimelineSetProperty(slot == 0 ? "props.audio.fadeInDuration" : "props.audio.fadeOutDuration", durationValue);
                sent[slot] = gSlT[gi];
            }
        }
        Clay_ElementId sid = CLAY_IDI("Dur", gi);
        Bs bs = Interact(sid);
        SliderLogic(sid, gi, 9.f);
        if (!gSlDrag[gi] && fabsf(gSlT[gi] - sent[slot]) > .002f) {
            PlutoTimelineSetProperty(slot == 0 ? "props.audio.fadeInDuration" : "props.audio.fadeOutDuration", 1.0 + gSlT[gi] * 9.0);
            sent[slot] = gSlT[gi];
        }
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(20)}},
                   .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD, gSl[gi],
                                               (bs.hover + (gSlDrag[gi] ? 1.f : 0.f)) * 1.5f, 0)}}) {}
    }
}

/* Audio page content (scrolling is handled by the shared left-panel scroller). */
static void AudioView(void) {
    CLAY(CLAY_ID("LibContent"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10, .padding = {0, 0, 0, 12}}}) {
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

#define LTAB_SLIDE_SECS 0.20f
#define LTAB_SLIDE_PX   36.f
static int gLTabDir = 1;
static float gLSbV = 0.f, gLSbF = 1.f;   /* scrollbar position / visible fraction for this frame */

/* Shared scroller for both left tabs: wheel + smooth follow + draggable scrollbar. Uses last frame's sizes. */
static void LeftScrollUpdate(void) {
    Clay_BoundingBox vb = Clay_GetElementData(CLAY_ID("LibScroll")).boundingBox;
    Clay_BoundingBox cb = Clay_GetElementData(CLAY_ID("LibContent")).boundingBox;
    Clay_BoundingBox bar = Clay_GetElementData(CLAY_ID("LibBar")).boundingBox;
    Clay_BoundingBox body = Clay_GetElementData(CLAY_ID("LibBody")).boundingBox;
    Vector2 m = GetMousePosition();
    gLTabT = fminf(1.f, gLTabT + fminf(gDt, 1.f / 30.f) / LTAB_SLIDE_SECS);
    { float u = 1.f - gLTabT; gLTabE = 1.f - u * u * u * u; }
    float maxS = fmaxf(0.f, cb.height - vb.height);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    float wh = gWheelDelta;
    if (wh != 0.f && !(ctrl && gLeftTab == 0) && body.width > 0.f &&
        m.x >= body.x && m.x <= body.x + body.width && m.y >= body.y && m.y <= body.y + body.height)
        gAudT -= wh * 60.f;

    static bool drag = false; static float grab = 0.f;
    float track = bar.height;
    float thumb = (maxS > 1.f && cb.height > 1.f) ? fmaxf(30.f, track * vb.height / cb.height) : track;
    if (maxS > 1.f && bar.height > 0.f) {
        Rectangle hit = {bar.x - 14.f, bar.y, bar.width + 20.f, bar.height};
        float thumbY = bar.y + (track - thumb) * (gAudS / maxS);
        if (CheckCollisionPointRec(m, hit)) gHand = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, hit)) {
            drag = true;
            grab = (m.y >= thumbY && m.y <= thumbY + thumb) ? m.y - thumbY : thumb * .5f;
        }
        if (drag) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float f = (m.y - grab - bar.y) / fmaxf(1.f, track - thumb);
                f = f < 0.f ? 0.f : f > 1.f ? 1.f : f;
                gAudT = gAudS = f * maxS;
            } else drag = false;
        }
    } else drag = false;
    if (gAudT > maxS) gAudT = maxS;
    if (gAudT < 0.f) gAudT = 0.f;
    if (!drag) gAudS += (gAudT - gAudS) * (1.f - expf(-20.f * gDt));
    if (fabsf(gAudT - gAudS) < 0.3f) gAudS = gAudT;
    if (gAudS > maxS) gAudS = maxS;
    gLSbV = maxS > 1.f ? gAudS / maxS : 0.f;
    gLSbF = maxS > 1.f && cb.height > 1.f ? vb.height / cb.height : 1.f;
}

static void LeftPanel(void) {
    /* Keep the panel geometry fixed while switching media/audio content. */
    gLFadeA = 0.f;
    LeftScrollUpdate();
    float pill = Anim(CLAY_ID("LibPillAnim").id, (float)gLeftTab, 20.f);

    CLAY(CLAY_ID("Library"), {.layout = {COLM, .sizing = {CLAY_SIZING_FIXED(gLeftW), GROW}, .padding = {13, 13, 12, 12}, .childGap = 12}, PANEL}) {
        CLAY(CLAY_ID("LibHead"), {.layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 0, 0}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_LIBRARY, 22, C_DIM);
            Tx("Library", 21.f, C_TEXT, 1);
            HSPACE
            Clay_ElementId imp = CLAY_ID("Import");
            Bs b = Interact(imp);
            if (b.clicked) PlutoRequestMediaImport();
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
                    gLTabDir = i > gLeftTab ? 1 : -1; gLeftTab = i; gLTabT = gLTabE = 0.f; gAudT = gAudS = 0.f;
                }
            }
        }
        CLAY(CLAY_ID("LibBody"), {.layout = {.sizing = {GROW, GROW}, .childGap = 6}}) {
            CLAY(CLAY_ID("LibScroll"), {.layout = {COLM, .sizing = {GROW, GROW}},
                                        .clip = {.horizontal = true, .vertical = true,
                                                 .childOffset = {gLTabDir * (1.f - gLTabE) * LTAB_SLIDE_PX, -gAudS}}}) {
                if (gLeftTab == 0) MediaView(); else AudioView();
            }
            CLAY(CLAY_ID("LibBar"), {.layout = {.sizing = {CLAY_SIZING_FIXED(3), GROW}},
                                     .custom = {.customData = NC(K_SCROLLBAR, C_CARD, C_CARD, C_CARD, gLSbV, gLSbF, 0)}}) {}
        }
    }
}

