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

static float gFxV[4][2];
static float gFxSent[4][2];
static bool gFxOn[4];
static const char *gExtraFxNames[] = {"Sharpen", "Lens Distortion", "Scanlines", "Camera Shake", "Film Burn", "Halftone", "Posterize", "Fisheye", "Light Leak", "Mirror"};
static const char *gExtraFxKeys[] = {"sharpen", "lens", "scanlines", "shake", "filmBurn", "halftone", "posterize", "fisheye", "lightLeak", "mirror"};
#define EXTRA_FX_COUNT 10
static bool gExtraFxOn[EXTRA_FX_COUNT];
static int gFilter;

static void PlutoHydrateInspector(const char *clipBegin, const char *clipEnd) {
#ifdef _WIN32
    static const char *sliderPaths[5][8] = {
        {0},
        {"hue", "saturation", "lightness"},
        {"props.colors.light.exposure", "props.colors.light.brightness", "props.colors.light.contrast", "props.colors.light.highlights", "props.colors.light.shadows", "props.colors.light.whites", "props.colors.light.blacks"},
        {"props.colors.color.temperature", "props.colors.color.vibrance", "props.colors.color.saturation"},
        {"props.colors.detail.sharpness", "props.colors.detail.clarity", "props.colors.detail.vignette"}
    };
    static const char *wheelNames[3] = {"shadows", "tint", "highlights"};
    static const char *presetNames[8] = {"none", "vivid", "noir", "fade", "warm", "cool", "cinema", "retro"};
    static const char *effectNames[4] = {"chromatic", "glow", "glitch", "pixelate"};
    static const char *audioPaths[4] = {"props.audio.volume", "props.audio.pan", "props.audio.bass", "props.audio.treble"};

    gNum[0] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.position.x", 0.f);
    gNum[1] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.position.y", 0.f);
    gNum[2] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.scale.w", 100.f);
    gNum[3] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.scale.h", 100.f);
    gRot = gRotT = PlutoReadNumber(clipBegin, clipEnd, "props.transform.rotation", 0.f);
    gSl[0] = gSlT[0] = fmaxf(0.f, fminf(1.f, PlutoReadNumber(clipBegin, clipEnd, "props.transform.opacity", 100.f) / 100.f));
    gLink[0] = false; gLink[1] = true; gLink[2] = false; gLink[3] = false;
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.transform.position.linked", &gLink[0]);
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.transform.scale.linked", &gLink[1]);
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.transform.crop.linked", &gLink[2]);
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.transform.distort.linked", &gLink[3]);

    static const char *miniPaths[8] = {
        "props.transform.crop.top", "props.transform.crop.left", "props.transform.crop.right", "props.transform.crop.bottom",
        "props.transform.distort.tl", "props.transform.distort.tr", "props.transform.distort.bl", "props.transform.distort.br"
    };
    for (int i = 0; i < 8; i++) gMini[i] = PlutoReadNumber(clipBegin, clipEnd, miniPaths[i], 0.f);

    gHsl = (int)roundf(PlutoReadNumber(clipBegin, clipEnd, "props.colors.hsl.selected", 0.f));
    if (gHsl < 0 || gHsl > 8) gHsl = 0;
    for (int group = 1; group < 5; group++) {
        for (int item = 0; item < 8 && sliderPaths[group][item]; item++) {
            char path[128];
            if (group == 1) snprintf(path, sizeof(path), "props.colors.hsl.%d.%s", gHsl, sliderPaths[group][item]);
            else snprintf(path, sizeof(path), "%s", sliderPaths[group][item]);
            float value = PlutoReadNumber(clipBegin, clipEnd, path, 0.f);
            float normalized;
            if (group == 1 && item == 0) normalized = value / 360.f + .5f;
            else if (group == 4 && item == 0) normalized = value / 100.f;
            else normalized = value / 200.f + .5f;
            int gi = group * 8 + item;
            gSl[gi] = gSlT[gi] = fmaxf(0.f, fminf(1.f, normalized));
        }
    }
    for (int i = 0; i < 3; i++) {
        char path[128];
        snprintf(path, sizeof(path), "props.colors.grading.%s.hue", wheelNames[i]);
        float hue = PlutoReadNumber(clipBegin, clipEnd, path, 0.f);
        snprintf(path, sizeof(path), "props.colors.grading.%s.amount", wheelNames[i]);
        float amount = PlutoReadNumber(clipBegin, clipEnd, path, 0.f) / 100.f;
        float angle = (hue - 90.f) * DEG2RAD;
        gWh[i] = gWhT[i] = (Vector2){cosf(angle) * amount, sinf(angle) * amount};
    }

    for (int i = 0; i < 4; i++) {
        float defaults[4][2] = {{.3f, .5f}, {.5f, .5f}, {.4f, .5f}, {.3f, .5f}};
        bool enabled = false;
        char path[128];
        snprintf(path, sizeof(path), "props.effects.%s.enabled", effectNames[i]);
        PlutoJsonBoolPath(clipBegin, clipEnd, path, &enabled);
        gFxOn[i] = enabled;
        snprintf(path, sizeof(path), "props.effects.%s.amount", effectNames[i]);
        if (i != 3) defaults[i][0] = PlutoReadNumber(clipBegin, clipEnd, path, defaults[i][0] * 100.f) / 100.f;
        else {
            snprintf(path, sizeof(path), "props.effects.%s.size", effectNames[i]);
            defaults[i][0] = PlutoReadNumber(clipBegin, clipEnd, path, defaults[i][0] * 100.f) / 100.f;
        }
        snprintf(path, sizeof(path), "props.effects.%s.%s", effectNames[i], i == 0 ? "angle" : i == 1 ? "radius" : i == 2 ? "speed" : "blend");
        float second = PlutoReadNumber(clipBegin, clipEnd, path, i == 0 ? 0.f : defaults[i][1] * 100.f);
        defaults[i][1] = i == 0 ? second / 360.f + .5f : second / 100.f;
        for (int p = 0; p < 2; p++) gFxV[i][p] = gFxSent[i][p] = fmaxf(0.f, fminf(1.f, defaults[i][p]));
    }
    for (int i = 0; i < EXTRA_FX_COUNT; i++) {
        char path[96]; snprintf(path, sizeof(path), "props.effects.%s.enabled", gExtraFxKeys[i]);
        gExtraFxOn[i] = false; PlutoJsonBoolPath(clipBegin, clipEnd, path, &gExtraFxOn[i]);
    }
    char preset[32] = {0};
    gFilter = 0;
    if (PlutoJsonStringPath(clipBegin, clipEnd, "props.effects.preset.id", preset, sizeof(preset)))
        for (int i = 0; i < 8; i++) if (strcmp(preset, presetNames[i]) == 0) gFilter = i;
    strcpy(gTransitionType, "none");
    gTransitionDuration = PlutoReadNumber(clipBegin, clipEnd, "props.transitionOut.duration", 1.f);
    PlutoJsonStringPath(clipBegin, clipEnd, "props.transitionOut.type", gTransitionType, sizeof(gTransitionType));

    for (int i = 0; i < 4; i++) {
        float value = PlutoReadNumber(clipBegin, clipEnd, audioPaths[i], i == 0 ? 100.f : 0.f);
        float normalized = i == 0 ? value / 200.f : value / (i == 1 ? 200.f : 24.f) + .5f;
        gKnob[i] = fmaxf(0.f, fminf(1.f, normalized));
    }
    gSwOn[0] = gSwOn[1] = false;
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.audio.fadeIn", &gSwOn[0]);
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.audio.fadeOut", &gSwOn[1]);
    gSl[40] = gSlT[40] = fmaxf(0.f, fminf(1.f, (PlutoReadNumber(clipBegin, clipEnd, "props.audio.fadeInDuration", 1.f) - 1.f) / 9.f));
    gSl[41] = gSlT[41] = fmaxf(0.f, fminf(1.f, (PlutoReadNumber(clipBegin, clipEnd, "props.audio.fadeOutDuration", 1.f) - 1.f) / 9.f));
#else
    (void)clipBegin; (void)clipEnd;
#endif
}

static void ResetAcc(int idx) {
    char updates[2048] = "[";
    size_t used = 1;
    bool first = true;
#define ADD_ZERO_PROPERTY(path) do { \
    int written = snprintf(updates + used, sizeof(updates) - used, "%s{\"path\":\"%s\",\"value\":0}", first ? "" : ",", (path)); \
    if (written > 0 && (size_t)written < sizeof(updates) - used) { used += (size_t)written; first = false; } \
} while (0)
    if (idx == 0) {
        static const char *channels[] = {"shadows", "tint", "highlights"};
        for (int i = 0; i < 3; i++) {
            gWhT[i] = (Vector2){0, 0};
            char path[112];
            snprintf(path, sizeof(path), "props.colors.grading.%s.hue", channels[i]); ADD_ZERO_PROPERTY(path);
            snprintf(path, sizeof(path), "props.colors.grading.%s.amount", channels[i]); ADD_ZERO_PROPERTY(path);
        }
    } else {
        for (int s = 0; s < 8; s++) gSlT[idx * 8 + s] = SlDefault(idx * 8 + s);
        if (idx == 1) {
            gHsl = 0;
            int written = snprintf(updates + used, sizeof(updates) - used,
                                   "{\"path\":\"props.colors.hsl\",\"value\":{}},{\"path\":\"props.colors.hsl.selected\",\"value\":0}");
            if (written > 0 && (size_t)written < sizeof(updates) - used) used += (size_t)written;
        } else {
            static const char *names[5][8] = {
                {0}, {0},
                {"exposure", "brightness", "contrast", "highlights", "shadows", "whites", "blacks"},
                {"temperature", "vibrance", "saturation"},
                {"sharpness", "clarity", "vignette"}
            };
            const char *group = idx == 2 ? "light" : idx == 3 ? "color" : "detail";
            for (int field = 0; field < 8 && names[idx][field]; field++) {
                char path[112];
                snprintf(path, sizeof(path), "props.colors.%s.%s", group, names[idx][field]);
                ADD_ZERO_PROPERTY(path);
            }
        }
    }
    if (used + 2 < sizeof(updates)) { updates[used++] = ']'; updates[used] = 0; PlutoTimelineSetPropertiesJson(updates); }
#undef ADD_ZERO_PROPERTY
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

static void CommitValueEdit(float *v) {
    if (gEditValueLen > 0) *v = strtof(gEditValueText, NULL);
    gEditValueId = 0;
}

static bool ValueFieldLogic(Clay_ElementId id, float *v, float dragSensitivity) {
    float original = *v;
    bool committed = false;
    bool valueChanged = false;
    bool over = Clay_PointerOver(id);
    if (over || gValueDragId == id.id) gScrubCursor = true;
    if (gEditValueId == id.id) {
        if (IsKeyPressed(KEY_ENTER)) { CommitValueEdit(v); committed = true; valueChanged = fabsf(*v - original) > 0.001f; }
        else if (IsKeyPressed(KEY_BACKSPACE) && gEditValueLen > 0) gEditValueText[--gEditValueLen] = 0;
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (((ch >= '0' && ch <= '9') || ch == '.' || ch == '-') && gEditValueLen < (int)sizeof(gEditValueText) - 1) {
                gEditValueText[gEditValueLen++] = (char)ch;
                gEditValueText[gEditValueLen] = 0;
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !over) { CommitValueEdit(v); committed = true; valueChanged = fabsf(*v - original) > 0.001f; }
    } else if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        gValueDragId = id.id;
        gValueDragging = false;
        gValueDragX = GetMousePosition().x;
        gValueDragStart = *v;
    }
    if (gValueDragId == id.id) {
        float dx = GetMousePosition().x - gValueDragX;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && fabsf(dx) > 3.f) {
            gValueDragging = true;
            *v = gValueDragStart + dx * dragSensitivity;
        } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (gValueDragging) { committed = true; valueChanged = fabsf(*v - gValueDragStart) > 0.001f; }
            if (!gValueDragging) {
                double now = GetTime();
                if (gLastValueClickId == id.id && now - gLastValueClickTime < 0.38) {
                    gEditValueId = id.id;
                    gEditValueLen = 0;
                    gEditValueText[0] = 0;
                }
                gLastValueClickId = id.id;
                gLastValueClickTime = now;
            }
            gValueDragId = 0;
            gValueDragging = false;
        }
    }
    return committed && valueChanged;
}

/* Value pills use their full hit area for click-to-type and horizontal scrubbing. */
static bool ValBoxEditable(Clay_ElementId id, float *value, const char *unit,
                           float w, float h, float fs, float ufs, float pad,
                           float minimum, float maximum, int decimals) {
    float dragSensitivity = fminf(.2f, fmaxf(.003f, (maximum - minimum) * .2f / 100.f));
    bool changed = ValueFieldLogic(id, value, dragSensitivity);
    *value = fmaxf(minimum, fminf(maximum, *value));
    bool over = Clay_PointerOver(id);
    CLAY(id, {.layout = {.sizing = {FIX(w), FIX(h)}, .padding = {0, pad, 0, 0}, .childGap = 5,
                         .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = Mix(C_VALBOX, RGB(47, 49, 55), over ? .28f : 0.f),
              .cornerRadius = CLAY_CORNER_RADIUS(12),
              .border = {.color = Mix(C_VALBOX, RGB(65, 67, 75), over ? .45f : 0.f),
                         .width = CLAY_BORDER_OUTSIDE(over ? 1 : 0)}}) {
        if (gEditValueId == id.id) Tx(Fmt("%s|", gEditValueText), fs + 2.f, C_TEXT, 0);
        else Tx(decimals > 0 ? Fmt("%.*f", decimals, *value) : Fmt("%.0f", *value), fs + 2.f, C_TEXT, 0);
        if (unit) Tx(unit, ufs + 1.f, C_ICON, 0);
    }
    return changed;
}

/* number field (GROW x 39): value + unit + up/down steppers */
static const char *kNumPropertyPaths[4] = {"props.transform.position.x", "props.transform.position.y", "props.transform.scale.w", "props.transform.scale.h"};
static int gLinkedNumDragField = -1;
static float gLinkedNumOtherStart;

static void CommitNumField(int n, float before) {
    if (n < 0 || n >= 4) return;
    int group = n < 2 ? 0 : 1;
    if (!gLink[group]) { PlutoTimelineSetProperty(kNumPropertyPaths[n], gNum[n]); return; }
    int other = n ^ 1;
    if (n >= 2) {
        float ratio = fabsf(before) > 0.0001f ? gNum[n] / before : 1.f;
        float otherStart = gLinkedNumDragField == n ? gLinkedNumOtherStart : gNum[other];
        gNum[other] = fmaxf(0.f, otherStart * ratio);
    } else gNum[other] = gNum[n];
    char updates[384];
    snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]",
             kNumPropertyPaths[n], gNum[n], kNumPropertyPaths[other], gNum[other]);
    PlutoTimelineSetPropertiesJson(updates);
    if (gLinkedNumDragField == n) gLinkedNumDragField = -1;
}

static void NumField(int n, float *v, const char *unit) {
    Clay_ElementId id = CLAY_IDI("NumF", n);
    Clay_ElementId scrubId = CLAY_IDI("NumScrub", n);
    Bs b = Interact(id);
    float before = gValueDragId == scrubId.id ? gValueDragStart : *v;
    bool changed = ValueFieldLogic(scrubId, v, .2f);
    int linkedGroup = n < 2 ? 0 : 1;
    bool linkedDrag = gLink[linkedGroup] && gValueDragId == scrubId.id && gValueDragging;
    if (linkedDrag) {
        int other = n ^ 1;
        if (gLinkedNumDragField != n) {
            gLinkedNumDragField = n;
            gLinkedNumOtherStart = gNum[other];
        }
        if (n < 2) gNum[other] = *v;
        else {
            float ratio = fabsf(before) > 0.0001f ? *v / before : 1.f;
            gNum[other] = fmaxf(0.f, gLinkedNumOtherStart * ratio);
        }
    }
    if (changed) CommitNumField(n, before);
    else if (gLinkedNumDragField == n && gValueDragId != scrubId.id) gLinkedNumDragField = -1;
    CLAY(id, {.layout = {.sizing = {GROW, FIX(39)}, .padding = {10, 5, 0, 0}, .childGap = 6, .childAlignment = AL_Y},
              .backgroundColor = Mix(C_RFIELD, RGB(46, 47, 54), b.hover * 0.45f), .cornerRadius = CLAY_CORNER_RADIUS(11),
              .border = {.color = Mix(C_RFIELD, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        CLAY(scrubId, {.layout = {.sizing = {GROW, GROW}, .childGap = 4, .childAlignment = AL_Y}}) {
            HSPACE
            Tx(gEditValueId == scrubId.id ? Fmt("%s|", gEditValueText) : Fmt("%d", (int)roundf(*v)), 22, C_TEXT, 0);
            Tx(unit, 17, C_ICON, 0);
        }
        GAPX(3)
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}}}) {
            if (Arrow(CLAY_IDI("NumU", n), I_CHEV_UP)) { float old = *v; *v += 1.f; CommitNumField(n, old); }
            if (Arrow(CLAY_IDI("NumD", n), I_CHEV_DN)) { float old = *v; *v -= 1.f; CommitNumField(n, old); }
        }
    }
}

/* small field used by crop / distort panels (T, L, R, B, TL, TR, BL, BR).
   Deliberately fixed-width: these stay compact and the spacers around them absorb extra width. */
static void MiniField(int n, const char *lab) {
    Clay_ElementId id = CLAY_IDI("MiniF", n);
    Clay_ElementId scrubId = CLAY_IDI("MiniScrub", n);
    Bs b = Interact(id);
    bool changed = ValueFieldLogic(scrubId, &gMini[n], .2f);
    static const char *paths[8] = {"props.transform.crop.top", "props.transform.crop.left", "props.transform.crop.right", "props.transform.crop.bottom",
                                   "props.transform.distort.tl", "props.transform.distort.tr", "props.transform.distort.bl", "props.transform.distort.br"};
    if (changed && n >= 0 && n < 8) {
        if (n < 4 && gLink[2]) {
            int other = (n == 0) ? 3 : (n == 3) ? 0 : (n == 1) ? 2 : 1;
            gMini[other] = gMini[n];
            char updates[384];
            snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]", paths[n], gMini[n], paths[other], gMini[other]);
            PlutoTimelineSetPropertiesJson(updates);
        } else if (n >= 4 && gLink[3]) {
            char updates[768] = "["; size_t used = 1;
            for (int i = 4; i < 8; i++) {
                gMini[i] = gMini[n];
                int written = snprintf(updates + used, sizeof(updates) - used, "%s{\"path\":\"%s\",\"value\":%.4f}", i == 4 ? "" : ",", paths[i], gMini[i]);
                if (written > 0 && (size_t)written < sizeof(updates) - used) used += (size_t)written;
            }
            if (used + 2 < sizeof(updates)) { updates[used++] = ']'; updates[used] = 0; PlutoTimelineSetPropertiesJson(updates); }
        } else PlutoTimelineSetProperty(paths[n], gMini[n]);
    }
    CLAY(id, {.layout = {.sizing = {FIX(112), FIX(34)}, .padding = {10, 6, 0, 0}, .childGap = 4, .childAlignment = AL_Y},
              .backgroundColor = Mix(C_RFIELD, RGB(46, 47, 54), b.hover * 0.45f), .cornerRadius = CLAY_CORNER_RADIUS(10),
              .border = {.color = Mix(C_RFIELD, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Tx(lab, 12, RGB(112, 124, 156), 1);
        CLAY(scrubId, {.layout = {.sizing = {GROW, GROW}, .childGap = 4, .childAlignment = AL_Y}}) {
            HSPACE
            Tx(gEditValueId == scrubId.id ? Fmt("%s|", gEditValueText) : Fmt("%.0f", gMini[n]), 18, C_TEXT, 0);
            Tx("px", 13, C_ICON, 0);
        }
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
            if (IconBtn(CLAY_IDI("Reset", n), I_RESET, 24, 13, RGB(104, 105, 112), false, 6)) {
                *a = def; *b = def;
                char updates[512];
                if (n == 0) {
                    snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]",
                             kNumPropertyPaths[0], *a, kNumPropertyPaths[1], *b);
                } else {
                    snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"props.transform.scale.linked\",\"value\":%s}]",
                             kNumPropertyPaths[2], *a, kNumPropertyPaths[3], *b, gLink[link] ? "true" : "false");
                }
                PlutoTimelineSetPropertiesJson(updates);
                gPreviewDirty = true;
            }
        }
        GAPY(6)
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(39)}, .childGap = 7, .childAlignment = AL_Y}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(22), FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { Tx(l1, 19, C_ICON, 0); }
            NumField(n * 2, a, unit);
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(34)}, .childAlignment = AL_C}}) {
            if (LinkBtn(CLAY_IDI("Link", n), gLink[link], 28, 24)) {
                gLink[link] = !gLink[link];
                PlutoTimelineSetBoolProperty(link == 0 ? "props.transform.position.linked" : "props.transform.scale.linked", gLink[link]);
            }
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
    CLAY(id, {.layout = {.sizing = {GROW, FIX(64)}, .padding = {10, 10, 0, 0}, .childGap = 8, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(base, hov, b), .cornerRadius = CLAY_CORNER_RADIUS(20),
              .border = {.color = Mix(C_RCARDB, RGB(80, 83, 92), fmaxf(b.hover * 0.72f, open * 0.8f)), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Icon(icon, 22, Mix(C_ICON, C_TEXT, open));
        Tx(label, 16, C_TEXT, 0);
        HSPACE
        Icon(open > 0.5f ? I_CHEV_DN : I_CHEV_R, 16, Mix(C_ICON, C_TEXT, open));
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
            if (LinkBtn(CLAY_ID("LinkCrop"), gLink[2], 30, 20)) {
                gLink[2] = !gLink[2]; PlutoTimelineSetBoolProperty("props.transform.crop.linked", gLink[2]);
            }
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
            if (LinkBtn(CLAY_ID("LinkDist"), gLink[3], 30, 20)) {
                gLink[3] = !gLink[3]; PlutoTimelineSetBoolProperty("props.transform.distort.linked", gLink[3]);
            }
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) { MiniField(6, "BL"); HSPACE MiniField(7, "BR"); }
    }
}

static void NavSection(void) {
    static int bodyKind = -1;
    if (gOpenTool >= 0) bodyKind = gOpenTool;
    float open = EaseIO(AnimLin(CLAY_ID("ToolBodyAnim").id, gOpenTool >= 0 ? 1.f : 0.f, 0.2f));
    CLAY(CLAY_ID("NavCol"), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        CLAY(CLAY_ID("NavRow"), {.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
            if (NavButton(0, I_CROP, "Crop Tools", gOpenTool == 0 ? open : 0.f)) { gOpenTool = (gOpenTool == 0) ? -1 : 0; gFollow = (gOpenTool == 0); }
            if (NavButton(1, I_DISTORT, "Distort", gOpenTool == 1 ? open : 0.f)) { gOpenTool = (gOpenTool == 1) ? -1 : 1; gFollow = (gOpenTool == 1); }
        }
        if (bodyKind >= 0 && open >= 0.005f) {
            float bodyHeight = bodyKind == 0 ? CROP_H : DIST_H;
            float wrapHeight = roundf(open * (bodyHeight + 10.f));
            CLAY(CLAY_ID("ToolBodyWrap"), {.layout = {COLM, .sizing = {GROW, FIX(wrapHeight)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                if (bodyKind == 0) CropBody(); else DistortBody();
            }
        }
    }
}

/* Transform tab. A flexible spacer positions the enlarged 66px nav buttons
   above a deliberate 26px bottom gap, matching the reference panel rhythm. */
static void TransformView(void) {
    static float sentRotation;
    static float sentOpacity = 1.f;
    CLAY(CLAY_ID("TfCol"), {.layout = {COLM, .sizing = {GROW, GROW}}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
            FieldCard(0, "Position", I_POS, "X", "Y", &gNum[0], &gNum[1], 0.f, "px", 0);
            FieldCard(1, "Scale", I_SCALE, "W", "H", &gNum[2], &gNum[3], 100.f, "%", 1);
        }
        GAPY(10)
        CLAY(CLAY_ID("RotCard"), {.layout = {COLM, .sizing = {GROW, FIX(160)}, .padding = {13, 13, 13, 0}, .childGap = 6}, RCARD}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(35)}, .childGap = 8, .childAlignment = AL_Y}}) {
                if (IconBtn(CLAY_ID("RotReset"), I_ROT, 34, 24, C_TEXT, false, 17)) { gRotT = 0.f; PlutoTimelineSetProperty("props.transform.rotation", 0.0); sentRotation = 0.f; }
                Tx("Rotation", 19, C_TEXT, 0);
                HSPACE
                float rotationValue = gRot;
                if (ValBoxEditable(CLAY_ID("RotationValue"), &rotationValue, "\xC2\xB0", 128, 35, 22, 18, 11, -180.f, 180.f, 1)) {
                    gRot = gRotT = rotationValue;
                    sentRotation = rotationValue;
                    PlutoTimelineSetProperty("props.transform.rotation", rotationValue);
                    gPreviewDirty = true;
                }
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
            if (!gRotDrag && fabsf(gRotT - sentRotation) > 0.01f) {
                PlutoTimelineSetProperty("props.transform.rotation", gRotT);
                sentRotation = gRotT;
            }
        }
        GAPY(10)
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(27)}, .childAlignment = AL_Y}}) {
                Tx("Opacity", 18, C_TEXT, 0);
                HSPACE
                float opacityValue = gSl[0] * 100.f;
                if (ValBoxEditable(CLAY_ID("OpacityValue"), &opacityValue, "%", 72, 27, 16, 13, 8, 0.f, 100.f, 0)) {
                    gSl[0] = gSlT[0] = opacityValue / 100.f;
                    PlutoTimelineSetProperty("props.transform.opacity", opacityValue);
                    sentOpacity = gSlT[0];
                }
            }
            Clay_ElementId sid = CLAY_ID("OpacitySlider");
            Bs b = Interact(sid);
            SliderLogic(sid, 0, 8.5f);
            if (!gSlDrag[0] && fabsf(gSlT[0] - sentOpacity) > 0.002f) {
                PlutoTimelineSetProperty("props.transform.opacity", gSlT[0] * 100.f);
                sentOpacity = gSlT[0];
            }
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
    static Vector2 sent[3];
    static bool sentInit;
    CLAY(CLAY_IDI("WheelCol", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId wid = CLAY_IDI("Wheel", i);
        Bs b = Interact(wid);
        WheelLogic(i, wid);
        if (!sentInit) { for (int j = 0; j < 3; j++) sent[j] = gWhT[j]; sentInit = true; }
        if (!gWhDrag[i] && (fabsf(gWhT[i].x - sent[i].x) + fabsf(gWhT[i].y - sent[i].y) > .004f)) {
            static const char *grade[3] = {"shadows", "tint", "highlights"};
            float hue, amount; WheelInfo(gWhT[i], &hue, &amount);
            char updates[256];
            snprintf(updates, sizeof(updates), "[{\"path\":\"props.colors.grading.%s.hue\",\"value\":%.4f},{\"path\":\"props.colors.grading.%s.amount\",\"value\":%.4f}]",
                     grade[i], hue, grade[i], amount * 100.f);
            PlutoTimelineSetPropertiesJson(updates);
            sent[i] = gWhT[i];
        }
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

static const char *gColorSliderKeys[5][8] = {
    {0}, {"hue", "saturation", "lightness"},
    {"exposure", "brightness", "contrast", "highlights", "shadows", "whites", "blacks"},
    {"temperature", "vibrance", "saturation"}, {"sharpness", "clarity", "vignette"}
};

static float ColorSliderFromDisplay(int idx, int s, float value) {
    if (idx == 1 && s == 0) return .5f + value / 360.f;
    if (idx == 4 && s == 0) return value / 100.f;
    return .5f + value / 200.f;
}

static double ColorSliderBackendValue(int idx, int s, float normalized) {
    if (idx == 1 && s == 0) return (normalized - .5f) * 360.0;
    if (idx == 4 && s == 0) return normalized * 100.0;
    return (normalized - .5f) * 200.0;
}

static void ColorSliderProperty(int idx, int s, char *path, size_t cap) {
    if (idx == 1) snprintf(path, cap, "props.colors.hsl.%d.%s", gHsl, gColorSliderKeys[idx][s]);
    else {
        const char *group = idx == 2 ? "light" : idx == 3 ? "color" : "detail";
        snprintf(path, cap, "props.colors.%s.%s", group, gColorSliderKeys[idx][s]);
    }
}
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
    static float sent[NSL];
    static bool sentInit;
    if (!sentInit) { memcpy(sent, gSlT, sizeof(sent)); sentInit = true; }
    int gi = idx * 8 + s, mode; Clay_Color a, b, c;
    SliderSpec(idx, s, &mode, &a, &b, &c);
    bool uni = (idx == 4 && s == 0);
    int val = uni ? (int)roundf(gSl[gi] * 100.f) :
              idx == 1 && s == 0 ? (int)roundf((gSl[gi] - .5f) * 360.f) : (int)roundf((gSl[gi] - .5f) * 200.f);
    CLAY(CLAY_IDI("SRow", gi), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 5}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx(kSliderNames[idx][s], 16.5f, C_DIM, 0);
            HSPACE
            float editValue = (float)val;
            if (ValBoxEditable(CLAY_IDI("ColorValue", gi), &editValue, NULL, 72, 26, 15, 0, 11,
                               idx == 1 && s == 0 ? -180.f : idx == 4 && s == 0 ? 0.f : -100.f,
                               idx == 1 && s == 0 ? 180.f : idx == 4 && s == 0 ? 100.f : 100.f, 0)) {
                float normalized = fmaxf(0.f, fminf(1.f, ColorSliderFromDisplay(idx, s, roundf(editValue))));
                gSl[gi] = gSlT[gi] = normalized;
                char path[128]; ColorSliderProperty(idx, s, path, sizeof(path));
                PlutoTimelineSetProperty(path, ColorSliderBackendValue(idx, s, normalized));
                sent[gi] = normalized;
            }
        }
        Clay_ElementId sid = CLAY_IDI("Slid", gi);
        Bs bs = Interact(sid);
        SliderLogic(sid, gi, 8.5f);
        if (!gSlDrag[gi] && fabsf(gSlT[gi] - sent[gi]) > .002f && idx > 0 && idx < 5) {
            char path[128];
            ColorSliderProperty(idx, s, path, sizeof(path));
            PlutoTimelineSetProperty(path, ColorSliderBackendValue(idx, s, gSlT[gi]));
            sent[gi] = gSlT[gi];
        }
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
    if (b.clicked) { gHsl = i; PlutoTimelineSetProperty("props.colors.hsl.selected", i); }
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
                            ResetIcon(rid, I_RESET, 14, rc, b.clicked);
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
        Accordion(0, "Color Grading", I_PALETTE);
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
    if (b.clicked) {
        static const char *presets[8] = {"none", "vivid", "noir", "fade", "warm", "cool", "cinema", "retro"};
        gFilter = i;
        PlutoTimelineSetStringProperty("props.effects.preset.id", presets[i]);
        PlutoTimelineSetProperty("props.effects.preset.intensity", 100.0);
    }
    CLAY(id, {.layout = {.sizing = {GROW, FIX(84)}},
              .custom = {.customData = NC(K_FILTER, kFilters[i].a, kFilters[i].b, kFilters[i].sun, 0, b.hover, gFilter == i)}}) {}
}

/* ---- video effects (accordion: one effect open at a time) ---- */
#define FX_ITEM_H 44.f
#define FX_BODY_H 160.f   /* 12 pad | 26 enabled row | 10 | 44 param | 10 | 44 param | 14 pad */
typedef struct { const char *name; const char *p1, *p2; bool angle; } FxDef;
static const FxDef kFx[4] = {
    {"Chromatic aberration", "Amount",    "Angle", true},
    {"Glow",                 "Intensity", "Radius", false},
    {"Glitch",               "Amount",    "Speed", false},
    {"Pixelate",             "Size",      "Blend", false},
};
static float gFxV[4][2] = {{.3f, .5f}, {.5f, .5f}, {.4f, .5f}, {.3f, .5f}};
static float gFxSent[4][2] = {{.3f, .5f}, {.5f, .5f}, {.4f, .5f}, {.3f, .5f}};
static const char *gFxProp[4][2] = {
    {"props.effects.chromatic.amount", "props.effects.chromatic.angle"},
    {"props.effects.glow.amount", "props.effects.glow.radius"},
    {"props.effects.glitch.amount", "props.effects.glitch.speed"},
    {"props.effects.pixelate.size", "props.effects.pixelate.blend"}
};
static const char *gFxToggle[4] = {"props.effects.chromatic.enabled", "props.effects.glow.enabled", "props.effects.glitch.enabled", "props.effects.pixelate.enabled"};
static bool  gFxOn[4];
static bool  gFxSlDrag[4][2];
static int   gFxOpen = 0;
static bool  gVfxOpen = true, gFiltOpen = false;

static void FxSliderLogic(Clay_ElementId id, float *v, bool *drag, float kr) {
    if (Clay_PointerOver(id) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = id.id;
    *drag = false;
    if (gDragId == id.id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Clay_BoundingBox bb = Clay_GetElementData(id).boundingBox;
            if (bb.width > 2 * kr) {
                float t = (GetMousePosition().x - bb.x - kr) / (bb.width - 2 * kr);
                *v = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
            }
            *drag = true;
        } else gDragId = 0;
    }
}

static void FxParam(int i, int p, const char *label) {
    float *v = &gFxV[i][p];
    bool isAngle = (p == 1 && kFx[i].angle);
    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(22)}, .childAlignment = AL_Y}}) {
            Tx(label, 13.5f, C_MUTED, 0);
            HSPACE
            float displayValue = isAngle ? roundf((*v - .5f) * 360.f) : roundf(*v * 100.f);
            if (ValBoxEditable(CLAY_IDI("FxValue", i * 2 + p), &displayValue,
                               isAngle ? "\xC2\xB0" : NULL, 72, 22, 13.f, 11.f, 10,
                               isAngle ? -180.f : 0.f, isAngle ? 180.f : 100.f, 0)) {
                *v = isAngle ? .5f + displayValue / 360.f : displayValue / 100.f;
                *v = fmaxf(0.f, fminf(1.f, *v));
                double stored = isAngle ? (*v - .5f) * 360.0 : *v * 100.0;
                PlutoTimelineSetProperty(gFxProp[i][p], stored);
                gFxSent[i][p] = *v;
            }
        }
        Clay_ElementId sid = CLAY_IDI("FxSl", i * 2 + p);
        Bs bs = Interact(sid);
        bool wasDragging = gFxSlDrag[i][p];
        FxSliderLogic(sid, v, &gFxSlDrag[i][p], 7.f);
        if (wasDragging && !gFxSlDrag[i][p] && fabsf(*v - gFxSent[i][p]) > .002f) {
            double value = (p == 1 && kFx[i].angle) ? (*v - .5f) * 360.0 : *v * 100.0;
            PlutoTimelineSetProperty(gFxProp[i][p], value);
            gFxSent[i][p] = *v;
        }
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(16)}},
                   .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD, *v,
                                               (bs.hover + (gFxSlDrag[i][p] ? 1.f : 0.f)) * 1.5f, 0)}}) {}
    }
}

static void FxBody(int i) {
    CLAY(CLAY_IDI("FxBody", i), {.layout = {COLM, .sizing = {GROW, FIX(FX_BODY_H)}, .padding = {14, 14, 12, 14}, .childGap = 10},
                                 .backgroundColor = C_RFIELD, .cornerRadius = CLAY_CORNER_RADIUS(14),
                                 .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(26)}, .childAlignment = AL_Y}}) {
            Clay_ElementId swid = CLAY_IDI("FxSw", i);
            Bs sb = Interact(swid);
            if (sb.clicked) {
                gFxOn[i] = !gFxOn[i];
                if (gFxOn[i]) {
                    double first = gFxV[i][0] * 100.0;
                    double second = i == 0 ? (gFxV[i][1] - .5f) * 360.0 : gFxV[i][1] * 100.0;
                    char updates[512];
                    snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":true},{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]",
                             gFxToggle[i], gFxProp[i][0], first, gFxProp[i][1], second);
                    PlutoTimelineSetPropertiesJson(updates);
                } else PlutoTimelineSetBoolProperty(gFxToggle[i], false);
            }
            float on = Anim(swid.id + 5u, gFxOn[i] ? 1.f : 0.f, 22.f);
            CLAY(swid, {.layout = {.sizing = {FIT, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(38), FIX(22)}},
                              .custom = {.customData = NC(K_SWITCH, C_CARD, C_CARD, C_CARD, on, sb.hover * 1.f - sb.press * 1.5f, 0)}}) {}
                Tx("Enabled", 13.5f, Mix(C_MUTED, C_DIM, sb.hover), 0);
            }
            HSPACE
            Clay_ElementId rid = CLAY_IDI("FxReset", i);
            Bs rb = Interact(rid);
            Clay_Color rc = Mix(RGB(140, 142, 150), C_TEXT, rb.hover);
            CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                ResetIcon(rid, I_RESET, 14, rc, rb.clicked);
                Tx("Reset", 14, rc, 0);
            }
            if (rb.clicked) {
                gFxV[i][0] = gFxSent[i][0] = (i == 0) ? .3f : i == 2 ? .4f : i == 3 ? .3f : .5f;
                gFxV[i][1] = gFxSent[i][1] = .5f;
                gFxOn[i] = false;
                char updates[512];
                snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":false},{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]",
                         gFxToggle[i], gFxProp[i][0], gFxV[i][0] * 100.0, gFxProp[i][1], i == 0 ? 0.0 : gFxV[i][1] * 100.0);
                PlutoTimelineSetPropertiesJson(updates);
            }
        }
        FxParam(i, 0, kFx[i].p1);
        FxParam(i, 1, kFx[i].p2);
    }
}

static void ExtraFxRow(int i) {
    Clay_ElementId id = CLAY_IDI("FxExtra", i);
    Bs b = Interact(id);
    if (b.clicked) {
        gExtraFxOn[i] = !gExtraFxOn[i];
        char path[96]; snprintf(path, sizeof(path), "props.effects.%s.enabled", gExtraFxKeys[i]);
        PlutoTimelineSetBoolProperty(path, gExtraFxOn[i]);
    }
    CLAY(id, {.layout = {.sizing = {GROW, FIX(FX_ITEM_H)}, .padding = {14, 10, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(C_RCARD, RGB(38, 40, 46), b), .cornerRadius = CLAY_CORNER_RADIUS(12),
              .border = {.color = Mix(C_RCARDB, RGB(70, 73, 81), b.hover * .55f), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Icon(I_SPARKLE, 18, gExtraFxOn[i] ? C_TEXT : C_ICON);
        Tx(gExtraFxNames[i], 15, C_TEXT, 0);
        HSPACE
        if (gExtraFxOn[i]) Icon(I_CHEV_DN, 14, C_TEXT); else Icon(I_PLUS, 14, C_ICON);
    }
}

/* Effect rows use one small sparkle mark to keep the list visually quiet. */
static bool FxHeader(int i, float open) {
    Clay_ElementId id = CLAY_IDI("FxHdr", i);
    Bs b = Interact(id);
    Clay_Color base = Mix(C_RCARD, RGB(42, 44, 51), open);
    Clay_Color hov  = Mix(RGB(38, 40, 46), RGB(48, 50, 58), open);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(FX_ITEM_H)}, .padding = {14, 10, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(base, hov, b), .cornerRadius = CLAY_CORNER_RADIUS(14),
              .border = {.color = Mix(C_RCARDB, RGB(80, 83, 92), fmaxf(b.hover * 0.72f, open * 0.8f)), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Icon(I_SPARKLE, 18, Mix(C_ICON, C_TEXT, open));
        Tx(kFx[i].name, 16, C_TEXT, 0);
        HSPACE
        Clay_Color gc = Mix(C_ICON, C_TEXT, fmaxf(b.hover, open));
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(24), FIX(24)}, .childAlignment = AL_C},
                      .cornerRadius = CLAY_CORNER_RADIUS(12),
                      .border = {.color = Mix(RGB(70, 72, 80), RGB(120, 123, 132), fmaxf(b.hover, open * 0.6f)), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            if (open > 0.5f) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(10), FIX(2)}}, .backgroundColor = gc, .cornerRadius = CLAY_CORNER_RADIUS(1)}) {}
            } else {
                Icon(I_PLUS, 12, gc);
            }
        }
    }
    return b.clicked;
}

#define TRANS_DESC_H 210.f
#define TRANS_EMPTY_H 46.f
#define FILT_BODY_H  214.f   /* 10 gap | 18 presets row | 10 gap | 2 x 84 thumbs + 8 gap */
static int TransitionNextClip(int from) {
    if (from < 0 || from >= gTimelineClipCount || gTimelineClips[from].track > 3) return -1;
    float end = gTimelineClips[from].start + gTimelineClips[from].duration;
    int best = -1;
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (i == from || gTimelineClips[i].track != gTimelineClips[from].track) continue;
        if (fabsf(gTimelineClips[i].start - end) <= .04f) { best = i; break; }
    }
    return best;
}

static void CommitTransition(void) {
    char updates[256];
    snprintf(updates, sizeof(updates), "[{\"path\":\"props.transitionOut.type\",\"value\":\"%s\"},{\"path\":\"props.transitionOut.duration\",\"value\":%.3f}]",
             gTransitionType, (double)gTransitionDuration);
    PlutoTimelineSetPropertiesJson(updates);
}

static void EffectsView(void) {
    /* ---- Transitions ---- */
    int from = gTransitionFocusClip >= 0 ? gTransitionFocusClip : gTimelineSelected;
    int next = TransitionNextClip(from);
    float o = EaseIO(AnimLin(CLAY_ID("TransAnim").id, gTransOpen ? 1.f : 0.f, 0.2f));
    float dh = roundf(o * (next >= 0 ? TRANS_DESC_H : TRANS_EMPTY_H));
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
                if (next < 0) {
                    TxWrap("Place a second video clip directly after the selected clip, then use the plus at their join.", 14, C_MUTED, 0);
                } else {
                    TxWrap(Fmt("%s to %s", gTimelineClips[from].label, gTimelineClips[next].label), 13, C_MUTED, 0);
                    const char *names[5] = {"None", "Cross dissolve", "Dip to black", "Wipe", "Iris"};
                    const char *keys[5] = {"none", "crossfade", "dip", "wipe", "iris"};
                    for (int row = 0; row < 3; row++) {
                        CLAY(CLAY_IDI("TransitionChoiceRow", row), {.layout = {.sizing = {GROW, FIX(34)}, .childGap = 8}}) {
                            for (int col = 0; col < 2; col++) {
                                int i = row * 2 + col;
                                if (i >= 5) break;
                                Clay_ElementId cid = CLAY_IDI("TransitionChoice", i);
                                Bs cb = Interact(cid);
                                bool selected = strcmp(gTransitionType, keys[i]) == 0;
                                if (cb.clicked) { strcpy(gTransitionType, keys[i]); CommitTransition(); }
                                CLAY(cid, {.layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C},
                                          .backgroundColor = BtnBg(selected ? C_SEL : C_CARD, RGB(43, 45, 52), cb),
                                          .cornerRadius = CLAY_CORNER_RADIUS(9),
                                          .border = {.color = Mix(C_CARDB, C_ACCENT, selected ? .45f : cb.hover * .35f), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                                    Tx(names[i], 13, selected ? C_TEXT : C_MUTED, 0);
                                }
                            }
                        }
                    }
                    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(22)}, .childAlignment = AL_Y}}) {
                        Tx("Duration", 13, C_MUTED, 0); HSPACE;
                        float durationValue = gTransitionDuration;
                        if (ValBoxEditable(CLAY_ID("TransitionDurationValue"), &durationValue, "s", 72, 22, 13, 11, 10, .1f, 2.f, 1)) {
                            gTransitionDuration = durationValue;
                            CommitTransition();
                        }
                    }
                    Clay_ElementId did = CLAY_ID("TransitionDuration");
                    static bool durationDrag;
                    if (Clay_PointerOver(did) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) durationDrag = true;
                    if (durationDrag && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                        Clay_BoundingBox bb = Clay_GetElementData(did).boundingBox;
                        if (bb.width > 1.f) gTransitionDuration = .1f + 1.9f * Clampf((GetMousePosition().x - bb.x) / bb.width, 0.f, 1.f);
                    }
                    if (durationDrag && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { durationDrag = false; CommitTransition(); }
                    CLAY(did, {.layout = {.sizing = {GROW, FIX(16)}},
                               .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD, (gTransitionDuration - .1f) / 1.9f, durationDrag ? 1.f : Clay_PointerOver(did), 0)}}) {}
                }
            }
        }
    }

    /* ---- Filters (collapsible; presets inside) ---- */
    float fo = EaseIO(AnimLin(CLAY_ID("FiltAnim").id, gFiltOpen ? 1.f : 0.f, 0.2f));
    bool fOpenFull = fo >= 0.999f;
    float fh = roundf(fo * FILT_BODY_H);
    CLAY(CLAY_ID("FiltersCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {14, 14, 14, 14}}, RCARD}) {
        Clay_ElementId fid = CLAY_ID("FiltersHdr");
        Bs b = Interact(fid);
        if (b.clicked) gFiltOpen = !gFiltOpen;
        CLAY(fid, {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_SPARKLE, 20, C_ICON);
            Tx("Filters", 16, C_TEXT, 1);
            HSPACE
            Icon(gFiltOpen ? I_CHEV_DN : I_CHEV_R, 14, Mix(C_MUTED, C_TEXT, b.hover));
        }
        if (fOpenFull || fh >= 1.f) {
            CLAY(CLAY_ID("FiltBody"), {.layout = {COLM, .sizing = {GROW, fOpenFull ? FIT : FIX(fh)}}, .clip = {.vertical = !fOpenFull}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
                    Tx("Filter presets", 13, C_MUTED, 0);
                    HSPACE
                    Clay_ElementId rid = CLAY_ID("FiltersReset");
                    Bs rb = Interact(rid);
                    Clay_Color rc = Mix(RGB(140, 142, 150), C_TEXT, rb.hover);
                    CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                        ResetIcon(rid, I_RESET, 14, rc, rb.clicked);
                        Tx("Reset", 14, rc, 0);
                    }
                    if (rb.clicked) {
                        gFilter = 0;
                PlutoTimelineSetPropertiesJson("[{\"path\":\"props.effects.preset.id\",\"value\":\"none\"},{\"path\":\"props.effects.preset.intensity\",\"value\":100}]");
                    }
                }
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_ID("FltRows"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 8}}) {
                    for (int row = 0; row < 2; row++) {
                        CLAY(CLAY_IDI("FltRow", row), {.layout = {.sizing = {GROW, FIT}, .childGap = 8}}) {
                            for (int c = 0; c < 4; c++) FilterThumb(row * 4 + c);
                        }
                    }
                }
            }
        }
    }

    /* ---- Video Effects ---- */
    float ov = EaseIO(AnimLin(CLAY_ID("VfxAnim").id, gVfxOpen ? 1.f : 0.f, 0.2f));
    float oi[4], hb[4], sum = 0.f;
    for (int i = 0; i < 4; i++) {
        oi[i] = Spring(CLAY_IDI("FxAnim", i).id, gFxOpen == i ? 1.f : 0.f, 680.f, 32.f);
        if (oi[i] < 0.f) oi[i] = 0.f;
        if (oi[i] > 1.04f) oi[i] = 1.04f;
        hb[i] = roundf(oi[i] * (FX_BODY_H + 8.f));
        sum += hb[i];
    }
    float lh = roundf(ov * (10.f + (4.f + EXTRA_FX_COUNT) * FX_ITEM_H + 13.f * 8.f + sum));
    CLAY(CLAY_ID("VfxCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {14, 14, 14, 14}}, RCARD}) {
        Clay_ElementId vid = CLAY_ID("VfxHdr");
        Bs vb = Interact(vid);
        if (vb.clicked) gVfxOpen = !gVfxOpen;
        CLAY(vid, {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_FILM, 20, C_ICON);
            Tx("Video Effects", 16, C_TEXT, 1);
            HSPACE
            Icon(gVfxOpen ? I_CHEV_DN : I_CHEV_R, 14, Mix(C_MUTED, C_TEXT, vb.hover));
        }
        if (lh >= 1.f) {
            CLAY(CLAY_ID("VfxWrap"), {.layout = {COLM, .sizing = {GROW, FIX(lh)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_ID("VfxList"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 8}}) {
                    for (int i = 0; i < 4; i++) {
                        CLAY(CLAY_IDI("FxItem", i), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
                            if (FxHeader(i, oi[i])) gFxOpen = (gFxOpen == i) ? -1 : i;
                            if (hb[i] >= 1.f) {
                                CLAY(CLAY_IDI("FxWrap", i), {.layout = {COLM, .sizing = {GROW, FIX(hb[i])}}, .clip = {.vertical = true}}) {
                                    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(8)}}}) {}
                                    FxBody(i);
                                }
                            }
                        }
                    }
                    for (int i = 0; i < EXTRA_FX_COUNT; i++) ExtraFxRow(i);
                }
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
    gTabT = fminf(1.f, gTabT + gDt / 0.29f);
    gTabE = gTabT * gTabT * (3.f - 2.f * gTabT);
    gFadeR = (Rectangle){bb.x, bb.y, bb.width, bb.height};
    gFadeA = (1.f - gTabE) * 255.f;
    float maxS = fmaxf(0.f, cb.height - bb.height);
    float wh = gWheelDelta;
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
                    if (TabBtn(CLAY_IDI("InspTab", i), tabs[i], gRightTab == i, 15.f, 11, true)) {
                        if (gRightTab != i) { gRightTab = i; gScroll = gScrollT = 0.f; gFollow = false; gTabT = 0.f; gTabE = 0.f; }
                    }
            }
        }
        CLAY(CLAY_ID("InspBody"), {.layout = {.sizing = {GROW, fitBody ? FIT : GROW}}, RPANEL,
                                   .clip = {.vertical = true, .childOffset = {0, -gScroll + (1.f - gTabE) * 18.f}}}) {
            CLAY(CLAY_ID("InspContent"), {.layout = {COLM, .sizing = {GROW, fitBody ? FIT : CLAY_SIZING_FIT(gInspViewH)}, .padding = {13, 13, 15, 13}, .childGap = 10}}) {
                if (gTimelineSelected < 0) {
                    CLAY(CLAY_ID("NoClip"), {.layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C}}) {
                        CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
                            Icon(I_FILM, 26, C_MUTED);
                            Tx("Nothing to adjust", 17, C_TEXT, 1);
                            TxWrap("Select a clip on the timeline to edit its properties.", 14, C_MUTED, 0);
                        }
                    }
                } else {
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
}

