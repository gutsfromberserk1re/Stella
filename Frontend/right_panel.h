/* Right panel: inspector, controls and inspector scrolling. */
/* ------------------------------------------------------------------ */
/* UI: inspector (right) - interaction helpers                         */
/* ------------------------------------------------------------------ */
static float SlDefault(int gi) {
    if (gi == 0) return 1.f;                 /* opacity */
    if (gi == 40 || gi == 41) return 0.f;    /* fade durations */
    if (gi / 8 == 4 && gi % 8 == 0) return 0.f; /* sharpness starts at 0 */
    return .5f;
}
static void ResetAcc(int idx) {
    if (idx == 0) { for (int i = 0; i < 3; i++) gWhT[i] = (Vector2){0, 0}; }
    else { for (int s = 0; s < 8; s++) gSlT[idx * 8 + s] = SlDefault(idx * 8 + s); if (idx == 1) gHsl = 0; }
}
static void TickState(void) {
    float k = 1.f - expf(-18.f * gDt);
    for (int i = 0; i < NSL; i++) {
        if (gSlDrag[i]) continue;
        gSl[i] += (gSlT[i] - gSl[i]) * k;
        if (fabsf(gSlT[i] - gSl[i]) < 0.0006f) gSl[i] = gSlT[i];
    }
    if (!gRotDrag) {
        gRot += (gRotT - gRot) * k;
        if (fabsf(gRotT - gRot) < 0.02f) gRot = gRotT;
    }
    for (int i = 0; i < 3; i++) {
        if (gWhDrag[i]) continue;
        gWh[i].x += (gWhT[i].x - gWh[i].x) * k; gWh[i].y += (gWhT[i].y - gWh[i].y) * k;
        if (fabsf(gWhT[i].x - gWh[i].x) < 0.002f && fabsf(gWhT[i].y - gWh[i].y) < 0.002f) gWh[i] = gWhT[i];
    }
}

/* drag a horizontal slider (value 0..1 in gSl[gi]) */
static void SliderLogic(Clay_ElementId id, int gi, float kr) {
    if (Clay_PointerOver(id) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = id.id;
    gSlDrag[gi] = false;
    if (gDragId == id.id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Clay_BoundingBox bb = Clay_GetElementData(id).boundingBox;
            if (bb.width > 2 * kr) {
                float t = (GetMousePosition().x - bb.x - kr) / (bb.width - 2 * kr);
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                gSl[gi] = gSlT[gi] = t;
            }
            gSlDrag[gi] = true;
        } else gDragId = 0;
    }
}

static void WheelLogic(int i, Clay_ElementId id) {
    Clay_BoundingBox bb = Clay_GetElementData(id).boundingBox;
    Vector2 m = GetMousePosition();
    float rad = fminf(bb.width, bb.height) / 2.f;
    Vector2 c = {bb.x + bb.width / 2.f, bb.y + bb.height / 2.f};
    float dx = m.x - c.x, dy = m.y - c.y;
    if (rad > 1.f && Clay_PointerOver(id) && sqrtf(dx * dx + dy * dy) <= rad + 6.f) {
        gCross = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragWheel = i;
    }
    gWhDrag[i] = false;
    if (gDragWheel == i) {
        gCross = true;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && rad > 1.f) {
            float px = dx / (rad - 1.f), py = dy / (rad - 1.f), l = sqrtf(px * px + py * py);
            if (l > 1.f) { px /= l; py /= l; }
            gWh[i] = gWhT[i] = (Vector2){px, py};
            gWhDrag[i] = true;
        } else gDragWheel = -1;
    }
}

/* stepper chevron (8 x 5 px ink) */
static bool Arrow(Clay_ElementId id, int icon) {
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {FIX(16), FIX(16)}, .childAlignment = AL_C}}) {
        Icon(icon, 16, Mix(C_ICON, C_TEXT, b.hover));
    }
    return b.clicked;
}

/* read-only value pill: value (fs) + optional unit (ufs), right aligned with `pad` px inset */
static void ValBox(const char *s, const char *unit, float w, float h, float fs, float ufs, float pad) {
    CLAY_AUTO_ID({.layout = {.sizing = {FIX(w), FIX(h)}, .padding = {0, pad, 0, 0}, .childGap = 5,
                             .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = C_VALBOX, .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
        Tx(s, fs, C_TEXT, 0);
        if (unit) Tx(unit, ufs, C_ICON, 0);
    }
}

/* number field (112 x 39): value + unit + up/down steppers */
static void NumField(int n, float *v, const char *unit) {
    Clay_ElementId id = CLAY_IDI("NumF", n);
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {FIX(112), FIX(39)}, .padding = {10, 5, 0, 0}, .childGap = 6, .childAlignment = AL_Y},
              .backgroundColor = Mix(Mix(C_RFIELD, RGB(46, 47, 54), b.hover), RGB(70, 73, 83), b.press * 0.8f), .cornerRadius = CLAY_CORNER_RADIUS(11),
              .border = {.color = Mix(C_RFIELD, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        HSPACE
        Tx(Fmt("%d", (int)roundf(*v)), 22, C_TEXT, 0);
        Tx(unit, 17, C_ICON, 0);
        GAPX(3)
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}}}) {
            if (Arrow(CLAY_IDI("NumU", n), I_CHEV_UP)) *v += 1.f;
            if (Arrow(CLAY_IDI("NumD", n), I_CHEV_DN)) *v -= 1.f;
        }
    }
}

/* small field used by crop / distort panels (T, L, R, B, TL, TR, BL, BR) */
static void MiniField(int n, const char *lab) {
    Clay_ElementId id = CLAY_IDI("MiniF", n);
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {FIX(112), FIX(34)}, .padding = {10, 6, 0, 0}, .childGap = 4, .childAlignment = AL_Y},
              .backgroundColor = Mix(Mix(C_RFIELD, RGB(46, 47, 54), b.hover), RGB(70, 73, 83), b.press * 0.8f), .cornerRadius = CLAY_CORNER_RADIUS(10),
              .border = {.color = Mix(C_RFIELD, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Tx(lab, 12, RGB(112, 124, 156), 1);
        HSPACE
        Tx("0", 18, C_TEXT, 0);
        Tx("px", 13, C_ICON, 0);
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}, .padding = {2, 0, 0, 0}}}) {
            Icon(I_CHEV_UP, 10, C_ICON);
            Icon(I_CHEV_DN, 10, C_ICON);
        }
    }
}

/* Position / Scale card: exactly 169 px tall
   12 pad | 24 header | 6 | 39 field | 34 link row | 39 field | 15 pad */
static void FieldCard(int n, const char *title, int icon, const char *l1, const char *l2, float *a, float *b, float def, const char *unit, int link) {
    CLAY(CLAY_IDI("FCard", n), {.layout = {COLM, .sizing = {GROW, FIX(169)}, .padding = {13, 13, 12, 0}}, RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(24)}, .padding = {3, 0, 0, 0}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(icon, 20, C_ICON);
            Tx(title, 19, C_TEXT, 1);
            HSPACE
            if (IconBtn(CLAY_IDI("Reset", n), I_RESET, 24, 13, RGB(104, 105, 112), false, 6)) { *a = def; *b = def; }
        }
        GAPY(6)
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(39)}, .childGap = 7, .childAlignment = AL_Y}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(22), FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { Tx(l1, 19, C_ICON, 0); }
            NumField(n * 2, a, unit);
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(34)}, .childAlignment = AL_C}}) {
            if (LinkBtn(CLAY_IDI("Link", n), gLink[link], 28, 24)) gLink[link] = !gLink[link];
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(39)}, .childGap = 7, .childAlignment = AL_Y}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(22), FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { Tx(l2, 19, C_ICON, 0); }
            NumField(n * 2 + 1, b, unit);
        }
    }
}

/* Crop Tools / Distort toggle button (66 px tall before FIX scaling) */
static bool NavButton(int n, int icon, const char *label, float open) {
    Clay_ElementId id = CLAY_IDI("Nav", n);
    Bs b = Interact(id);
    Clay_Color base = Mix(C_RCARD, RGB(42, 44, 51), open);
    Clay_Color hov  = Mix(RGB(38, 40, 46), RGB(48, 50, 58), open);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(64)}, .padding = {16, 18, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(base, hov, b), .cornerRadius = CLAY_CORNER_RADIUS(20),
              .border = {.color = Mix(C_RCARDB, RGB(80, 83, 92), fmaxf(b.hover, open * 0.8f)), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Icon(icon, 29, Mix(C_ICON, C_TEXT, open));
        Tx(label, 19, C_TEXT, 0);
        HSPACE
        Icon(open > 0.5f ? I_CHEV_DN : I_CHEV_R, 20, Mix(C_ICON, C_TEXT, open));
    }
    return b.clicked;
}

#define CROP_H 146.f
#define DIST_H 142.f

static void CropBody(void) {
    CLAY(CLAY_ID("CropBody"), {.layout = {COLM, .sizing = {GROW, FIX(CROP_H)}, .padding = CLAY_PADDING_ALL(12), .childGap = 10,
                                          .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}, RCARD}) {
        MiniField(0, "T");
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            MiniField(1, "L");
            HSPACE
            if (LinkBtn(CLAY_ID("LinkCrop"), gLink[2], 30, 20)) gLink[2] = !gLink[2];
            HSPACE
            MiniField(2, "R");
        }
        MiniField(3, "B");
    }
}

static void DistortBody(void) {
    CLAY(CLAY_ID("DistBody"), {.layout = {COLM, .sizing = {GROW, FIX(DIST_H)}, .padding = CLAY_PADDING_ALL(12), .childGap = 10}, RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) { MiniField(4, "TL"); HSPACE MiniField(5, "TR"); }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
            if (LinkBtn(CLAY_ID("LinkDist"), gLink[3], 30, 20)) gLink[3] = !gLink[3];
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) { MiniField(6, "BL"); HSPACE MiniField(7, "BR"); }
    }
}

static void NavSection(void) {
    float o0 = EaseIO(AnimLin(CLAY_ID("ExpCrop").id, gOpenTool == 0 ? 1.f : 0.f, 0.2f));
    float o1 = EaseIO(AnimLin(CLAY_ID("ExpDist").id, gOpenTool == 1 ? 1.f : 0.f, 0.2f));
    float h0 = roundf(o0 * (CROP_H + 10.f));
    float h1 = roundf(o1 * (DIST_H + 10.f));
    CLAY(CLAY_ID("NavCol"), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        CLAY(CLAY_ID("NavRow"), {.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
            if (NavButton(0, I_CROP, "Crop Tools", o0)) { gOpenTool = (gOpenTool == 0) ? -1 : 0; gFollow = (gOpenTool == 0); }
            if (NavButton(1, I_DISTORT, "Distort", o1)) { gOpenTool = (gOpenTool == 1) ? -1 : 1; gFollow = (gOpenTool == 1); }
        }
        if (h0 >= 1.f) {
            CLAY(CLAY_ID("CropWrap"), {.layout = {COLM, .sizing = {GROW, FIX(h0)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CropBody();
            }
        }
        if (h1 >= 1.f) {
            CLAY(CLAY_ID("DistWrap"), {.layout = {COLM, .sizing = {GROW, FIX(h1)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                DistortBody();
            }
        }
    }
}

static const char *RotStr(float r) { return fabsf(r) < 0.05f ? "0" : Fmt("%.1f", r); }

/* Transform tab. A flexible spacer positions the enlarged 66px nav buttons
   above a deliberate 26px bottom gap, matching the reference panel rhythm. */
static void TransformView(void) {
    CLAY(CLAY_ID("TfCol"), {.layout = {COLM, .sizing = {GROW, GROW}}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
            FieldCard(0, "Position", I_POS, "X", "Y", &gNum[0], &gNum[1], 0.f, "px", 0);
            FieldCard(1, "Scale", I_SCALE, "W", "H", &gNum[2], &gNum[3], 100.f, "%", 1);
        }
        GAPY(10)
        CLAY(CLAY_ID("RotCard"), {.layout = {COLM, .sizing = {GROW, FIX(160)}, .padding = {13, 13, 13, 0}, .childGap = 6}, RCARD}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(35)}, .childGap = 8, .childAlignment = AL_Y}}) {
                if (IconBtn(CLAY_ID("RotReset"), I_ROT, 34, 24, C_TEXT, false, 17)) gRotT = 0.f;
                Tx("Rotation", 19, C_TEXT, 0);
                HSPACE
                ValBox(RotStr(gRot), "\xC2\xB0", 128, 35, 22, 18, 11);
            }
            Clay_ElementId rid = CLAY_ID("RotRuler");
            Bs b = Interact(rid);
            if (Clay_PointerOver(rid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = rid.id;
            gRotDrag = false;
            if (gDragId == rid.id) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    Clay_BoundingBox bb = Clay_GetElementData(rid).boundingBox;
                    float span = bb.width - RULER_L - RULER_R;
                    if (span > 8) {
                        float t = (GetMousePosition().x - (bb.x + RULER_L)) / span;
                        t = t < 0 ? 0 : t > 1 ? 1 : t;
                        gRot = gRotT = -180.f + 360.f * t;
                    }
                    gRotDrag = true;
                } else gDragId = 0;
            }
            CLAY(rid, {.layout = {.sizing = {GROW, FIX(93)}}, .custom = {.customData = NC(K_ROTRULER, C_CARD, C_CARD, C_CARD, (gRot + 180.f) / 360.f, b.hover, 0)}}) {}
        }
        GAPY(10)
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(27)}, .childAlignment = AL_Y}}) {
                Tx("Opacity", 18, C_TEXT, 0);
                HSPACE
                ValBox(Fmt("%d", (int)roundf(gSl[0] * 100.f)), "%", 72, 27, 16, 13, 8);
            }
            Clay_ElementId sid = CLAY_ID("OpacitySlider");
            Bs b = Interact(sid);
            SliderLogic(sid, 0, 8.5f);
            CLAY(sid, {.layout = {.sizing = {GROW, FIX(26)}},
                       .custom = {.customData = NC(K_SLIDER, RGB(38, 39, 43), RGB(150, 152, 160), C_CARD, gSl[0], (b.hover + (gSlDrag[0] ? 1.f : 0.f)) * 1.5f, 1)}}) {}
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, CLAY_SIZING_GROW(10)}}}) {}   /* min 10px, grows when the window is taller */
        NavSection();
        GAPY(26)
    }
}

/* ---------------- colors tab ---------------- */
static void WheelItem(int i, const char *label) {
    CLAY(CLAY_IDI("WheelCol", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId wid = CLAY_IDI("Wheel", i);
        Bs b = Interact(wid);
        WheelLogic(i, wid);
        float hue, len; WheelInfo(gWh[i], &hue, &len);
        bool col = len > 0.02f;
        Color dc = ColorFromHSV(hue, fminf(1.f, .3f + .7f * len), 1.f);
        CLAY(wid, {.layout = {.sizing = {FIX(100), FIX(100)}},
                   .custom = {.customData = NC(K_WHEEL, C_CARD, (Clay_Color){gWh[i].x, gWh[i].y, 0, 0}, (Clay_Color){dc.r, dc.g, dc.b, 255},
                                               col ? 1.f : 0.f, fmaxf(b.hover * .6f, gWhDrag[i] ? 1.f : 0.f), 0)}}) {}
        Tx(label, 16, C_TEXT, 1);
        if (col) Tx(Fmt("%d\xC2\xB0 \xC2\xB7 %d", (int)roundf(hue) % 360, (int)roundf(fminf(1.f, len) * 100.f)), 13, RGB(140, 142, 150), 0);
        else     Tx("Neutral", 13, RGB(140, 142, 150), 0);
    }
}

static void AccHeader(int idx, const char *title, int icon, float openAnim, bool modified) {
    Clay_ElementId id = CLAY_IDI("AccHdr", idx);
    Bs b = Interact(id);
    if (b.clicked) {
        if (gOpenAcc == idx) { gOpenAcc = -1; gFollow = false; }
        else { gOpenAcc = idx; gFollow = (idx >= 3); }
    }

    /* press-squash: shrinks while held, springs back on release */
    bool down = Clay_PointerOver(id) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float sq = Spring(id.id, down ? 1.f : 0.f, 700.f, 26.f);
    sq = sq < 0.f ? 0.f : sq > 1.2f ? 1.2f : sq;
    int px = (int)roundf(sq * 4.f), py = (int)roundf(sq * 1.5f);

    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(48)}, .padding = {px, px, py, py}}}) {
        CLAY(id, {.layout = {.sizing = {GROW, GROW}, .padding = {16, 14, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
                  /* hover only, no press flash */
                  .backgroundColor = Mix(Mix(C_RCARD, RGB(33, 34, 38), openAnim), Mix(RGB(36, 37, 43), RGB(40, 41, 47), openAnim), b.hover),
                  .cornerRadius = CLAY_CORNER_RADIUS(18),
                  .border = {.color = Mix(Mix(C_RCARDB, RGB(62, 64, 72), openAnim), RGB(84, 87, 96), b.hover * 0.7f),
                             .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Icon(icon, 22, C_ICON);
            Tx(title, 18, C_TEXT, 0);
            HSPACE
            if (modified) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(6), FIX(6)}}, .backgroundColor = C_TEXT, .cornerRadius = CLAY_CORNER_RADIUS(3)}) {}
                GAPX(2)
            }
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(16), FIX(16)}, .childAlignment = AL_C}, .cornerRadius = CLAY_CORNER_RADIUS(9),
                          .border = {.color = Mix(RGB(74, 77, 85), RGB(130, 134, 144), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                IconV(I_PLUS, 10, C_DIM, 1.f - openAnim);
            }
        }
    }
}

static const char *kSliderNames[5][8] = {
    {0}, {"Hue", "Saturation", "Lightness"},
    {"Exposure", "Brightness", "Contrast", "Highlights", "Shadows", "Whites", "Blacks"},
    {"Temperature", "Vibrance", "Saturation"}, {"Sharpness", "Clarity", "Vignette"}
};
/* last measured body heights (fallbacks until first measure) */
static float gAccH[5] = {250.f, 270.f, 410.f, 190.f, 190.f};

static void SliderSpec(int idx, int s, int *mode, Clay_Color *a, Clay_Color *b, Clay_Color *c) {
    /* default for every plain slider: dark track + light progress bar from the centre to the knob */
    *mode = 8; *a = RGB(38, 39, 43); *b = RGB(150, 152, 160); *c = *b;
    if (idx == 4 && s == 0) *mode = 0;                 /* Sharpness is 0..100: progress from the left */
    if (idx == 1) {                                    /* HSL keeps its colour bars */
        if (s == 0) *mode = 4;
        else if (s == 1) { *mode = 2; *a = RGB(96, 96, 102); *b = RGB(236, 64, 80); }
        else             { *mode = 2; *a = RGB(8, 8, 10);    *b = RGB(240, 240, 244); }
    } else if (idx == 3 && s == 0) {                   /* Temperature: blue -> white -> orange */
        *mode = 2 | 16; *a = RGB(66, 134, 238); *c = RGB(226, 228, 236); *b = RGB(246, 170, 56);
    }
}

static void SliderRow(int idx, int s) {
    int gi = idx * 8 + s, mode; Clay_Color a, b, c;
    SliderSpec(idx, s, &mode, &a, &b, &c);
    bool uni = (idx == 4 && s == 0);
    int val = uni ? (int)roundf(gSl[gi] * 100.f) : (int)roundf((gSl[gi] - .5f) * 200.f);
    CLAY(CLAY_IDI("SRow", gi), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 5}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx(kSliderNames[idx][s], 16.5f, C_DIM, 0);
            HSPACE
            ValBox(Fmt("%d", val), NULL, 72, 26, 15, 0, 11);
        }
        Clay_ElementId sid = CLAY_IDI("Slid", gi);
        Bs bs = Interact(sid);
        SliderLogic(sid, gi, 8.5f);
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(14)}},
                   .custom = {.customData = NC(K_SLIDER, a, b, c, gSl[gi], (bs.hover + (gSlDrag[gi] ? 1.f : 0.f)) * 1.5f, 1 | mode)}}) {}
    }
}

static void Chip(int i) {
    static const Clay_Color cc[9] = {{0,0,0,255}, {239,68,68,255}, {249,115,22,255}, {234,179,8,255}, {34,197,94,255},
                                     {6,182,212,255}, {59,130,246,255}, {139,92,246,255}, {236,72,153,255}};
    Clay_ElementId id = CLAY_IDI("Chip", i);
    Bs b = Interact(id);
    float sel = Anim(id.id + 3u, gHsl == i ? 1.f : 0.f, 30.f);
    if (b.clicked) gHsl = i;
    CLAY(id, {.layout = {.sizing = {FIX(30), FIX(30)}, .childAlignment = AL_C}, .cornerRadius = CLAY_CORNER_RADIUS(15),
              .border = {.color = RGBA(240, 242, 246, 255.f * sel + 70.f * b.hover * (1.f - sel)), .width = CLAY_BORDER_OUTSIDE(2)}}) {
        float sz = 22.f + 2.f * b.hover - 2.f * b.press;
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(sz), FIX(sz)}}, .custom = {.customData = NC(K_CHIP, cc[i], cc[i], cc[i], 0, 0, i == 0)}}) {}
    }
}

static void Accordion(int idx, const char *title, int icon) {
    Clay_ElementId hid = CLAY_IDI("AccHdr", idx);
    float o = EaseIO(AnimLin(hid.id, gOpenAcc == idx ? 1.f : 0.f, 0.2f));
    float mh = Clay_GetElementData(CLAY_IDI("AccBody", idx)).boundingBox.height;
    if (mh > 1.f) gAccH[idx] = mh;
    float wrapH = roundf(o * (gAccH[idx] + 10.f));
    int n = 0;
    if (idx > 0) while (n < 7 && kSliderNames[idx][n]) n++;
    bool modified = false;
    if (idx == 0) { for (int i = 0; i < 3; i++) if (fabsf(gWhT[i].x) + fabsf(gWhT[i].y) > 0.02f) modified = true; }
    else for (int s2 = 0; s2 < n; s2++) if (fabsf(gSlT[idx * 8 + s2] - SlDefault(idx * 8 + s2)) > 0.01f) modified = true;
    CLAY(CLAY_IDI("AccCol", idx), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        AccHeader(idx, title, icon, o, modified);
        if (wrapH >= 1.f) {
            CLAY(CLAY_IDI("AccWrap", idx), {.layout = {COLM, .sizing = {GROW, FIX(wrapH)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_IDI("AccBody", idx), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(12), .childGap = 10}, RCARD}) {
                    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT}}}) {
                        Clay_ElementId rid = CLAY_IDI("AccReset", idx);
                        Bs b = Interact(rid);
                        Clay_Color rc = Mix(RGB(140, 142, 150), C_TEXT, b.hover);
                        CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                            Icon(I_RESET, 14, rc);
                            Tx("Reset", 13, rc, 0);
                        }
                        if (b.clicked) ResetAcc(idx);
                    }
                    if (idx == 0) {
                        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
                            WheelItem(0, "Shadows"); WheelItem(1, "Tint"); WheelItem(2, "Highlights");
                        }
                    } else {
                        if (idx == 1) {
                            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 6}}) {
                                for (int c = 0; c < 9; c++) Chip(c);
                            }
                        }
                        for (int s = 0; s < n; s++) SliderRow(idx, s);
                    }
                }
            }
        }
    }
}

static void ColorsView(void) {
    CLAY(CLAY_ID("AccList"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10}}) {
        Accordion(0, "Color Grading", I_GRADING);
        Accordion(1, "HSL", I_SLIDERS);
        Accordion(2, "Light", I_SUN);
        Accordion(3, "Color", I_DROP);
        Accordion(4, "Detail", I_MOUNTAIN);
    }
}

/* ---------------- effects tab ---------------- */
typedef struct { Clay_Color a, b, sun; } FilterP;
static const FilterP kFilters[8] = {
    {{236,110,96,255},  {84,66,208,255},  {250,226,140,255}},
    {{255,150,40,255},  {70,88,232,255},  {255,222,120,255}},
    {{158,158,164,255}, {38,38,44,255},   {240,240,244,255}},
    {{232,160,136,255}, {120,98,192,255}, {250,226,150,255}},
    {{60,176,204,255},  {38,48,124,255},  {250,236,170,255}},
    {{128,196,112,255}, {28,86,80,255},   {250,236,160,255}},
    {{204,80,164,255},  {58,30,112,255},  {252,214,150,255}},
    {{242,204,100,255}, {156,66,52,255},  {255,244,200,255}},
};

static void FilterThumb(int i) {
    Clay_ElementId id = CLAY_IDI("Flt", i);
    Bs b = Interact(id);
    if (b.clicked) gFilter = i;
    CLAY(id, {.layout = {.sizing = {GROW, FIX(84)}},
              .custom = {.customData = NC(K_FILTER, kFilters[i].a, kFilters[i].b, kFilters[i].sun, 0, b.hover, gFilter == i)}}) {}
}

#define TRANS_DESC_H 46.f
static void EffectsView(void) {
    float o = EaseIO(AnimLin(CLAY_ID("TransAnim").id, gTransOpen ? 1.f : 0.f, 0.2f));
    float dh = roundf(o * TRANS_DESC_H);
    CLAY(CLAY_ID("TransCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {14, 14, 14, 14}}, RCARD}) {
        Clay_ElementId hid = CLAY_ID("TransHdr");
        Bs b = Interact(hid);
        if (b.clicked) gTransOpen = !gTransOpen;
        CLAY(hid, {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_BOWTIE, 20, C_ICON);
            Tx("Transitions", 16, C_TEXT, 1);
            HSPACE
            Icon(gTransOpen ? I_CHEV_DN : I_CHEV_R, 14, Mix(C_MUTED, C_TEXT, b.hover));
        }
        if (dh >= 1.f) {
            CLAY(CLAY_ID("TransDesc"), {.layout = {COLM, .sizing = {GROW, FIX(dh)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(8)}}}) {}
                TxWrap("Place another clip right after this one to add a transition between them.", 14, C_MUTED, 0);
            }
        }
    }
    CLAY(CLAY_ID("FiltersCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {14, 14, 14, 14}, .childGap = 10}, RCARD}) {
        Clay_ElementId fid = CLAY_ID("FiltersHdr");
        Bs b = Interact(fid);
        CLAY(fid, {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_SPARKLE, 20, C_ICON);
            Tx("Filters", 16, C_TEXT, 1);
            HSPACE
            Icon(I_CHEV_R, 14, Mix(C_MUTED, C_TEXT, b.hover));
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx("Filter presets", 13, C_MUTED, 0);
            HSPACE
            Clay_ElementId rid = CLAY_ID("FiltersReset");
            Bs rb = Interact(rid);
            Clay_Color rc = Mix(RGB(140, 142, 150), C_TEXT, rb.hover);
            CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                Icon(I_RESET, 14, rc);
                Tx("Reset", 14, rc, 0);
            }
            if (rb.clicked) gFilter = 0;
        }
        for (int row = 0; row < 2; row++) {
            CLAY(CLAY_IDI("FltRow", row), {.layout = {.sizing = {GROW, FIT}, .childGap = 8}}) {
                for (int c = 0; c < 4; c++) FilterThumb(row * 4 + c);
            }
        }
    }
}

/* ---------------- text tab ---------------- */
static void TextView(void) {
    CLAY(CLAY_ID("SubCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(12), .childGap = 8}, RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_FILM, 20, C_ICON);
            Tx("Subtitles", 16, C_TEXT, 1);
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {2, 0, 2, 0}}}) { Tx("Caption", 13, C_MUTED, 0); }
        Clay_ElementId id = CLAY_ID("SubField");
        Bs b = Interact(id);
        CLAY(id, {.layout = {.sizing = {GROW, FIX(40)}, .padding = {12, 12, 0, 0}, .childAlignment = AL_Y},
                  .backgroundColor = Mix(RGB(28, 29, 33), RGB(36, 37, 43), b.hover), .cornerRadius = CLAY_CORNER_RADIUS(10),
                  .border = {.color = Mix(RGB(28, 29, 33), RGB(60, 62, 70), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Tx("Type a subtitle...", 14, RGB(112, 115, 124), 0);
        }
    }
}

/* scrolling for the inspector body (wheel + smooth follow + scrollbar thumb) */
static void UpdateInspScroll(void) {
    Clay_BoundingBox bb = Clay_GetElementData(CLAY_ID("InspBody")).boundingBox;
    Clay_BoundingBox cb = Clay_GetElementData(CLAY_ID("InspContent")).boundingBox;
    gInspViewH = bb.height;
    gTabT = fminf(1.f, gTabT + gDt / 0.24f);
    gTabE = gTabT * gTabT * (3.f - 2.f * gTabT);
    gFadeR = (Rectangle){bb.x, bb.y, bb.width, bb.height};
    gFadeA = (1.f - gTabE) * 255.f;
    float maxS = fmaxf(0.f, cb.height - bb.height);
    float wh = GetMouseWheelMove();
    Vector2 m = GetMousePosition();
    if (wh != 0.f && m.x >= bb.x && m.x <= bb.x + bb.width && m.y >= bb.y && m.y <= bb.y + bb.height) {
        gScrollT -= wh * 60.f;
        gFollow = false;
    }
    if (gFollow) gScrollT = maxS;
    if (gScrollT > maxS) gScrollT = maxS;
    if (gScrollT < 0.f) gScrollT = 0.f;
    gScroll += (gScrollT - gScroll) * (1.f - expf(-20.f * gDt));
    if (fabsf(gScrollT - gScroll) < 0.3f) gScroll = gScrollT;
    if (gScroll > maxS) gScroll = maxS;
    gSB = (maxS > 1.f && bb.width > 0.f);
    if (gSB) {
        float top = bb.y + 10.f, track = bb.height - 20.f;
        float thumb = fmaxf(36.f, track * bb.height / cb.height);
        float frac = gScroll / maxS;
        gSBR = (Rectangle){bb.x + bb.width - 9.f, top + (track - thumb) * frac, 4.f, thumb};
    }
}

static void RightPanel(void) {
    static const char *tabs[4] = {"Transform", "Colors", "Effects", "Text"};
    static const int icons[4] = {I_CROP, I_PALETTE, I_SUN, I_TEXT};
    float pill = Anim(CLAY_ID("PillAnim").id, (float)gRightTab, 20.f);
    bool fitBody = (gRightTab == 3);
    CLAY(CLAY_ID("Right"), {.layout = {COLM, .sizing = {CLAY_SIZING_FIXED(gRightW), GROW}, .childGap = 10}}) {
        /* tab bar: 54 tall = 6 pad + 42 track (4 pad + 34 pill + 4 pad) + 6 pad */
        CLAY(CLAY_ID("InspTabs"), {.layout = {.sizing = {GROW, FIX(54)}, .padding = CLAY_PADDING_ALL(6)}, RPANEL_R(18)}) {
            CLAY(CLAY_ID("InspSeg"), {.layout = {.sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(4), .childGap = 4},
                                      .cornerRadius = CLAY_CORNER_RADIUS(14),
                                      .custom = {.customData = NC(K_PILL, C_CARD, C_CARD, C_CARD, pill, 0, 0)}}) {
                for (int i = 0; i < 4; i++)
                    if (TabBtn(CLAY_IDI("InspTab", i), tabs[i], gRightTab == i, 13.5f, 11, true)) {
                        if (gRightTab != i) { gRightTab = i; gScroll = gScrollT = 0.f; gFollow = false; gTabT = 0.f; gTabE = 0.f; }
                    }
            }
        }
        CLAY(CLAY_ID("InspBody"), {.layout = {.sizing = {GROW, fitBody ? FIT : GROW}}, RPANEL,
                                   .clip = {.vertical = true, .childOffset = {0, -gScroll + (1.f - gTabE) * 18.f}}}) {
            CLAY(CLAY_ID("InspContent"), {.layout = {COLM, .sizing = {GROW, fitBody ? FIT : CLAY_SIZING_FIT(gInspViewH)}, .padding = {13, 13, 15, 13}, .childGap = 10}}) {
                if (gRightTab != 2) {
                    CLAY(CLAY_ID("InspTitle"), {.layout = {.sizing = {GROW, FIT}, .padding = {5, 0, 0, 5}, .childGap = 12, .childAlignment = AL_Y}}) {
                        Icon(icons[gRightTab], 26, C_ICON);
                        Tx(tabs[gRightTab], 23, C_TEXT, 1);
                    }
                }
                if (gRightTab == 0) TransformView();
                else if (gRightTab == 1) ColorsView();
                else if (gRightTab == 2) EffectsView();
                else TextView();
            }
        }
    }
}

