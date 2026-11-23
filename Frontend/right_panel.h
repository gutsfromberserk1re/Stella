/* Right panel: inspector, controls and inspector scrolling. */
/* ------------------------------------------------------------------ */
/* UI: inspector (right) - interaction helpers                         */
/* ------------------------------------------------------------------ */
/* Clay_SID expects a Clay_String, not a raw C string. Build a short-lived
   non-owning key for runtime helper arguments such as TextApply/TextBold. */
static Clay_ElementId TextDynamicElementId(const char *name) {
    if (!name) name = "";
    Clay_String key = {
        .isStaticallyAllocated = false,
        .length = (int32_t)strlen(name),
        .chars = name
    };
    return Clay__HashString(key, 0, 0);
}

static float SlDefault(int gi) {
    if (gi == 0) return 1.f;                 /* opacity */
    if (gi == 40 || gi == 41) return 0.f;    /* fade durations */
    if (gi / 8 == 4 && gi % 8 == 0) return 0.f; /* sharpness starts at 0 */
    return .5f;
}

static float gFxV[4][2];
static float gFxSent[4][2];
static bool gFxOn[4];
static const char *gExtraFxNames[] = {"Blur", "Film Grain", "Vignette", "Sharpen", "Lens Distortion", "Scanlines", "Camera Shake", "Film Burn", "Halftone", "Posterize", "Fisheye", "Light Leak", "Mirror"};
static const char *gExtraFxKeys[] = {"blur", "grain", "vignette", "sharpen", "lens", "scanlines", "shake", "filmBurn", "halftone", "posterize", "fisheye", "lightLeak", "mirror"};
#define EXTRA_FX_COUNT 13
static bool gExtraFxOn[EXTRA_FX_COUNT];
static int gFilter;

static int PlutoTextColorChannel(float value) {
    return (int)fmaxf(0.f, fminf(255.f, roundf(value)));
}

static Color PlutoReadTextColor(const char *begin, const char *end, const char *prefix, Color fallback) {
    char path[96];
    Color result = fallback;
    snprintf(path, sizeof(path), "props.text.%sR", prefix);
    result.r = (unsigned char)PlutoTextColorChannel(PlutoReadNumber(begin, end, path, fallback.r));
    snprintf(path, sizeof(path), "props.text.%sG", prefix);
    result.g = (unsigned char)PlutoTextColorChannel(PlutoReadNumber(begin, end, path, fallback.g));
    snprintf(path, sizeof(path), "props.text.%sB", prefix);
    result.b = (unsigned char)PlutoTextColorChannel(PlutoReadNumber(begin, end, path, fallback.b));
    snprintf(path, sizeof(path), "props.text.%sA", prefix);
    result.a = (unsigned char)PlutoTextColorChannel(PlutoReadNumber(begin, end, path, fallback.a));
    return result;
}

/* Convert a saved RGB colour back to the same hue/saturation disk used by the
   Colors tab. Neutral colours sit at the centre of the wheel. */
static Vector2 PlutoTextColorToWheel(Color color) {
    float r = color.r / 255.f, g = color.g / 255.f, b = color.b / 255.f;
    float hi = fmaxf(r, fmaxf(g, b)), lo = fminf(r, fminf(g, b));
    float d = hi - lo, hue = 0.f, sat = hi > .0001f ? d / hi : 0.f;
    if (d > .0001f) {
        if (hi == r) hue = 60.f * fmodf((g - b) / d, 6.f);
        else if (hi == g) hue = 60.f * ((b - r) / d + 2.f);
        else hue = 60.f * ((r - g) / d + 4.f);
    }
    if (hue < 0.f) hue += 360.f;
    float amount = fminf(.92f, sat * .92f);
    float angle = (hue - 90.f) * DEG2RAD;
    return (Vector2){cosf(angle) * amount, sinf(angle) * amount};
}

static void PlutoTextWheelApplyPosition(int index, Vector2 position) {
    if (index < 0 || index >= 4) return;
    float amount = sqrtf(position.x * position.x + position.y * position.y);
    if (amount < .025f) {
        gTextAppearance[index] = gTextAppearanceDefaults[index];
        return;
    }
    float hue = 0.f, ignored = 0.f;
    WheelInfo(position, &hue, &ignored);
    Color selected = ColorFromHSV(hue, fminf(1.f, .30f + .70f * amount), 1.f);
    selected.a = gTextAppearanceDefaults[index].a;
    gTextAppearance[index] = selected;
}

/* Resolve inspector transform fields from the selected clip's current keyframe
   segment. The preview uses the same linear interpolation in Rust/WGPU. */
static float PlutoKeyframedInspectorValue(const TimelineClipUi *clip, int field, float fallback, float at) {
    if (!clip || field < 0 || field >= 6) return fallback;
    unsigned bit = 1u << field;
    float firstTime = 0.f, firstValue = fallback, lastTime = 0.f, lastValue = fallback;
    bool haveFirst = false, haveLast = false;
    for (int i = 0; i < clip->keyframeCount; i++) {
        const TimelineKeyframeUi *key = &clip->keyframes[i];
        if (!(key->valueMask & bit)) continue;
        if (!haveFirst) { firstTime = lastTime = key->time; firstValue = lastValue = key->value[field]; haveFirst = haveLast = true; continue; }
        if (at <= key->time) {
            float span = key->time - lastTime;
            float t = span > 0.000001f ? fmaxf(0.f, fminf(1.f, (at - lastTime) / span)) : 1.f;
            return lastValue + (key->value[field] - lastValue) * t;
        }
        lastTime = key->time;
        lastValue = key->value[field];
        haveLast = true;
    }
    if (!haveFirst) return fallback;
    return at < firstTime ? firstValue : lastValue;
}

static void PlutoApplyKeyframeValues(void) {
    if (gTimelineSelected < 0 || gTimelineSelected >= gTimelineClipCount) return;
    const TimelineClipUi *clip = &gTimelineClips[gTimelineSelected];
    gNum[0] = PlutoKeyframedInspectorValue(clip, 0, gNum[0], gPlayhead);
    gNum[1] = PlutoKeyframedInspectorValue(clip, 1, gNum[1], gPlayhead);
    gNum[2] = PlutoKeyframedInspectorValue(clip, 2, gNum[2], gPlayhead);
    gNum[3] = PlutoKeyframedInspectorValue(clip, 3, gNum[3], gPlayhead);
    gRot = gRotT = PlutoKeyframedInspectorValue(clip, 4, gRotT, gPlayhead);
    float opacity = PlutoKeyframedInspectorValue(clip, 5, gSlT[0] * 100.f, gPlayhead);
    gSl[0] = gSlT[0] = fmaxf(0.f, fminf(1.f, opacity / 100.f));
}

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
    static const char *presetNames[6] = {"none", "vivid", "noir", "fade", "warm", "cool"};
    static const char *effectNames[4] = {"chromatic", "glow", "glitch", "pixelate"};
    static const char *audioPaths[4] = {"props.audio.volume", "props.audio.pan", "props.audio.bass", "props.audio.treble"};

    gNum[0] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.position.x", 0.f);
    gNum[1] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.position.y", 0.f);
    gNum[2] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.scale.w", 100.f);
    gNum[3] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.scale.h", 100.f);
    gRot = gRotT = PlutoReadNumber(clipBegin, clipEnd, "props.transform.rotation", 0.f);
    gSl[0] = gSlT[0] = fmaxf(0.f, fminf(1.f, PlutoReadNumber(clipBegin, clipEnd, "props.transform.opacity", 100.f) / 100.f));
    PlutoApplyKeyframeValues();
    gLink[0] = false; gLink[1] = true; gLink[2] = false; gLink[3] = false;
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.transform.position.linked", &gLink[0]);
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.transform.scale.linked", &gLink[1]);
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.transform.crop.linked", &gLink[2]);
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.transform.distort.linked", &gLink[3]);

    static const char *miniPaths[8] = {
        "props.transform.crop.top", "props.transform.crop.left", "props.transform.crop.right", "props.transform.crop.bottom",
        "props.transform.distort.tl", "props.transform.distort.tr", "props.transform.distort.bl", "props.transform.distort.br"
    };
    for (int i = 0; i < 8; i++) {
        float stored = PlutoReadNumber(clipBegin, clipEnd, miniPaths[i], 0.f);
        /* Crop is displayed as a percentage of the 1920x1080 reference canvas;
           the renderer and project format continue storing reference pixels. */
        gMini[i] = i < 4 ? stored * 100.f / ((i == 0 || i == 3) ? 1080.f : 1920.f) : stored;
        if (i < 4) gMini[i] = fmaxf(0.f, fminf(45.f, gMini[i]));
    }

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
        for (int i = 0; i < 6; i++) if (strcmp(preset, presetNames[i]) == 0) gFilter = i;
    gFilterIntensity = gFilterIntensitySent = Clampf(PlutoReadNumber(clipBegin, clipEnd, "props.effects.preset.intensity", 100.f) / 100.f, 0.f, 1.f);
    strcpy(gTransitionType, "none");
    gTransitionDuration = PlutoReadNumber(clipBegin, clipEnd, "props.transitionOut.duration", 1.f);
    PlutoJsonStringPath(clipBegin, clipEnd, "props.transitionOut.type", gTransitionType, sizeof(gTransitionType));

    gTextTitleSelected = false;
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.isTitle", &gTextTitleSelected);
    if (gTextTitleSelected) {
        PlutoJsonStringPath(clipBegin, clipEnd, "props.text.content", gTextContent, sizeof(gTextContent));
        gTextFontSize = (int)roundf(PlutoReadNumber(clipBegin, clipEnd, "props.text.fontSize", 96.f));
        gTextFontSize = (int)Clampf((float)gTextFontSize, 24.f, 180.f);
        gTextColorIndex = (int)roundf(PlutoReadNumber(clipBegin, clipEnd, "props.text.color", 0.f));
        if (gTextColorIndex < 0 || gTextColorIndex > 7) gTextColorIndex = 0;
        gTextAppearance[0] = PlutoReadTextColor(clipBegin, clipEnd, "ink", gStellaTextColors[gTextColorIndex]);
        gTextAppearance[1] = PlutoReadTextColor(clipBegin, clipEnd, "outline", gTextAppearanceDefaults[1]);
        gTextAppearance[2] = PlutoReadTextColor(clipBegin, clipEnd, "shadow", gTextAppearanceDefaults[2]);
        gTextAppearance[3] = PlutoReadTextColor(clipBegin, clipEnd, "background", gTextAppearanceDefaults[3]);
        for (int colorWheel = 0; colorWheel < 4; colorWheel++)
            gTextWheelPuck[colorWheel] = gTextWheelPuckT[colorWheel] = PlutoTextColorToWheel(gTextAppearance[colorWheel]);
        gTextAlign = (int)roundf(PlutoReadNumber(clipBegin, clipEnd, "props.text.align", 1.f));
        if (gTextAlign < 0 || gTextAlign > 2) gTextAlign = 1;
        gTextPosX = Clampf(PlutoReadNumber(clipBegin, clipEnd, "props.text.x", .5f), .03f, .97f);
        gTextPosY = Clampf(PlutoReadNumber(clipBegin, clipEnd, "props.text.y", .78f), .04f, .96f);
        gTextDuration = Clampf(PlutoReadNumber(clipBegin, clipEnd, "dur", gTextDuration), .5f, 30.f);
        gTextBold = true; gTextOutline = false; gTextShadow = true; gTextBackground = false;
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.bold", &gTextBold);
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.outline", &gTextOutline);
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.shadow", &gTextShadow);
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.background", &gTextBackground);
    }

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
    float wheelFollow = 1.f - expf(-14.f * gDt);
    for (int i = 0; i < 3; i++) {
        gWh[i].x += (gWhT[i].x - gWh[i].x) * wheelFollow;
        gWh[i].y += (gWhT[i].y - gWh[i].y) * wheelFollow;
        if (fabsf(gWhT[i].x - gWh[i].x) < 0.001f && fabsf(gWhT[i].y - gWh[i].y) < 0.001f) gWh[i] = gWhT[i];
    }
    for (int i = 0; i < 4; i++) {
        gTextWheelPuck[i].x += (gTextWheelPuckT[i].x - gTextWheelPuck[i].x) * wheelFollow;
        gTextWheelPuck[i].y += (gTextWheelPuckT[i].y - gTextWheelPuck[i].y) * wheelFollow;
        if (fabsf(gTextWheelPuckT[i].x - gTextWheelPuck[i].x) < .001f &&
            fabsf(gTextWheelPuckT[i].y - gTextWheelPuck[i].y) < .001f)
            gTextWheelPuck[i] = gTextWheelPuckT[i];
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
    static double lastClickTime[3];
    static Vector2 pressPosition[3];
    static bool pressMoved[3];
    Clay_BoundingBox bb = Clay_GetElementData(id).boundingBox;
    Vector2 m = GetMousePosition();
    float rad = fminf(bb.width, bb.height) / 2.f;
    Vector2 c = {bb.x + bb.width / 2.f, bb.y + bb.height / 2.f};
    float dx = m.x - c.x, dy = m.y - c.y;
    bool overWheel = rad > 1.f && Clay_PointerOver(id) && sqrtf(dx * dx + dy * dy) <= rad + 6.f;
    if (overWheel) {
        gCross = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            double now = GetTime();
            if (lastClickTime[i] > 0.0 && now - lastClickTime[i] < 0.38 && !pressMoved[i]) {
                gWhT[i] = (Vector2){0.f, 0.f};
                gWhResetPending[i] = true;
                lastClickTime[i] = 0.0;
                gDragWheel = -1;
            } else {
                gDragWheel = i;
                pressPosition[i] = m;
                pressMoved[i] = false;
                lastClickTime[i] = now;
            }
        }
    }
    gWhDrag[i] = false;
    if (gDragWheel == i) {
        gCross = true;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && rad > 1.f) {
            if (fabsf(m.x - pressPosition[i].x) + fabsf(m.y - pressPosition[i].y) > 2.5f)
                pressMoved[i] = true;
            float px = dx / (rad - 1.f), py = dy / (rad - 1.f), l = sqrtf(px * px + py * py);
            if (l > 1.f) { px /= l; py /= l; }
            gWhT[i] = (Vector2){px * 0.92f, py * 0.92f};
            gWhDrag[i] = true;
        } else {
            if (pressMoved[i]) lastClickTime[i] = 0.0;
            gDragWheel = -1;
        }
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

static bool ValueFieldLogicEx(Clay_ElementId id, float *v, float dragSensitivity, bool allowNegative) {
    float original = *v;
    bool committed = false;
    bool valueChanged = false;
    bool over = Clay_PointerOver(id);
    if (dragSensitivity >= 0.f && (over || gValueDragId == id.id)) gScrubCursor = true;
    if (gEditValueId == id.id) {
        if (IsKeyPressed(KEY_ENTER)) { CommitValueEdit(v); committed = true; valueChanged = fabsf(*v - original) > 0.001f; }
        else if (IsKeyPressed(KEY_BACKSPACE) && gEditValueLen > 0) gEditValueText[--gEditValueLen] = 0;
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (((ch >= '0' && ch <= '9') || ch == '.' || (allowNegative && ch == '-' && gEditValueLen == 0)) &&
                gEditValueLen < (int)sizeof(gEditValueText) - 1) {
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
        if (dragSensitivity >= 0.f && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && fabsf(dx) > 3.f) {
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

static bool ValueFieldLogic(Clay_ElementId id, float *v, float dragSensitivity) {
    return ValueFieldLogicEx(id, v, dragSensitivity, true);
}

/* Value pills share one visual component; callers choose whether mouse scrubbing is enabled. */
static bool ValBoxInternal(Clay_ElementId id, float *value, const char *unit,
                           float w, float h, float fs, float ufs, float pad,
                           float minimum, float maximum, int decimals, float dragSensitivity) {
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

static bool ValBoxEditable(Clay_ElementId id, float *value, const char *unit,
                           float w, float h, float fs, float ufs, float pad,
                           float minimum, float maximum, int decimals) {
    float dragSensitivity = fminf(.2f, fmaxf(.003f, (maximum - minimum) * .2f / 100.f));
    return ValBoxInternal(id, value, unit, w, h, fs, ufs, pad, minimum, maximum, decimals, dragSensitivity);
}

static bool ValBoxTyped(Clay_ElementId id, float *value, const char *unit,
                        float w, float h, float fs, float ufs, float pad,
                        float minimum, float maximum, int decimals) {
    return ValBoxInternal(id, value, unit, w, h, fs, ufs, pad, minimum, maximum, decimals, -1.f);
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
    bool changed = ValueFieldLogic(scrubId, v, .65f);
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
    if (gValueDragging && gValueDragId == scrubId.id) {
        if (gLink[linkedGroup]) {
            char updates[384];
            snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]",
                     kNumPropertyPaths[n], gNum[n], kNumPropertyPaths[n ^ 1], gNum[n ^ 1]);
            PlutoTimelineSetPropertiesLiveJson(updates);
        } else {
            PlutoTimelineSetPropertyLive(kNumPropertyPaths[n], *v);
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
    bool changed = ValueFieldLogicEx(scrubId, &gMini[n], 1.55f, n >= 4);
    static const char *paths[8] = {"props.transform.crop.top", "props.transform.crop.left", "props.transform.crop.right", "props.transform.crop.bottom",
                                   "props.transform.distort.tl", "props.transform.distort.tr", "props.transform.distort.bl", "props.transform.distort.br"};
    if (n >= 0 && n < 4) gMini[n] = fmaxf(0.f, fminf(45.f, gMini[n]));
    else if (n >= 4 && n < 8) gMini[n] = fmaxf(-2000.f, fminf(2000.f, gMini[n]));
    float storeValue = n < 4 ? gMini[n] * ((n == 0 || n == 3) ? 1080.f : 1920.f) / 100.f : gMini[n];
    if (gValueDragging && gValueDragId == scrubId.id && n >= 0 && n < 8) {
        if (n < 4 && gLink[2]) {
            int other = (n == 0) ? 3 : (n == 3) ? 0 : (n == 1) ? 2 : 1;
            gMini[other] = gMini[n];
            char updates[384];
            snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]",
                     paths[n], storeValue, paths[other], (other < 4 ? gMini[other] * ((other == 0 || other == 3) ? 1080.f : 1920.f) / 100.f : gMini[other]));
            PlutoTimelineSetPropertiesLiveJson(updates);
        } else if (n >= 4 && gLink[3]) {
            char updates[768] = "["; size_t used = 1;
            for (int i = 4; i < 8; i++) {
                gMini[i] = gMini[n];
                int written = snprintf(updates + used, sizeof(updates) - used, "%s{\"path\":\"%s\",\"value\":%.4f}", i == 4 ? "" : ",", paths[i], gMini[i]);
                if (written > 0 && (size_t)written < sizeof(updates) - used) used += (size_t)written;
            }
            if (used + 2 < sizeof(updates)) { updates[used++] = ']'; updates[used] = 0; PlutoTimelineSetPropertiesLiveJson(updates); }
        } else PlutoTimelineSetPropertyLive(paths[n], storeValue);
    }
    if (changed && n >= 0 && n < 8) {
        if (n < 4 && gLink[2]) {
            int other = (n == 0) ? 3 : (n == 3) ? 0 : (n == 1) ? 2 : 1;
            gMini[other] = gMini[n];
            char updates[384];
            snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]", paths[n], storeValue, paths[other], (other < 4 ? gMini[other] * ((other == 0 || other == 3) ? 1080.f : 1920.f) / 100.f : gMini[other]));
            PlutoTimelineSetPropertiesJson(updates);
        } else if (n >= 4 && gLink[3]) {
            char updates[768] = "["; size_t used = 1;
            for (int i = 4; i < 8; i++) {
                gMini[i] = gMini[n];
                int written = snprintf(updates + used, sizeof(updates) - used, "%s{\"path\":\"%s\",\"value\":%.4f}", i == 4 ? "" : ",", paths[i], gMini[i]);
                if (written > 0 && (size_t)written < sizeof(updates) - used) used += (size_t)written;
            }
            if (used + 2 < sizeof(updates)) { updates[used++] = ']'; updates[used] = 0; PlutoTimelineSetPropertiesJson(updates); }
        } else PlutoTimelineSetProperty(paths[n], storeValue);
    }
    CLAY(id, {.layout = {.sizing = {FIX(104), FIX(32)}, .padding = {8, 5, 0, 0}, .childGap = 4, .childAlignment = AL_Y},
              .backgroundColor = Mix(C_RFIELD, RGB(46, 47, 54), b.hover * 0.45f), .cornerRadius = CLAY_CORNER_RADIUS(10),
              .border = {.color = Mix(C_RFIELD, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Tx(lab, 12, RGB(112, 124, 156), 1);
        CLAY(scrubId, {.layout = {.sizing = {GROW, GROW}, .childGap = 4, .childAlignment = AL_Y}}) {
            HSPACE
            Tx(gEditValueId == scrubId.id ? Fmt("%s|", gEditValueText) : (n < 4 ? Fmt("%.1f", gMini[n]) : Fmt("%.0f", gMini[n])), 18, C_TEXT, 0);
            Tx(n < 4 ? "%" : "px", 13, C_ICON, 0);
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
        CLAY_AUTO_ID({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = {GROW, FIX(34)}, .padding = {29, 0, 0, 0}, .childAlignment = AL_C}}) {
            HSPACE
            if (LinkBtn(CLAY_IDI("Link", n), gLink[link], 34, 32)) {
                gLink[link] = !gLink[link];
                PlutoTimelineSetBoolProperty(link == 0 ? "props.transform.position.linked" : "props.transform.scale.linked", gLink[link]);
            }
            HSPACE
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
            if (LinkBtn(CLAY_ID("LinkCrop"), gLink[2], 38, 29)) {
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
            if (LinkBtn(CLAY_ID("LinkDist"), gLink[3], 38, 29)) {
                gLink[3] = !gLink[3]; PlutoTimelineSetBoolProperty("props.transform.distort.linked", gLink[3]);
            }
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) { MiniField(6, "BL"); HSPACE MiniField(7, "BR"); }
    }
}

static void NavSection(void) {
    static int bodyKind = -1;
    static int pendingTool = -1;
    if (gOpenTool >= 0) bodyKind = gOpenTool;
    float open = EaseIO(AnimLin(CLAY_ID("ToolBodyAnim").id, gOpenTool >= 0 ? 1.f : 0.f, 0.2f));
    if (pendingTool >= 0 && open < 0.005f) {
        gOpenTool = pendingTool;
        pendingTool = -1;
        gFollow = true;
    }
    if (gOpenTool >= 0 && pendingTool < 0) bodyKind = gOpenTool;
    CLAY(CLAY_ID("NavCol"), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        CLAY(CLAY_ID("NavRow"), {.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
            if (NavButton(0, I_CROP, "Crop Tools", gOpenTool == 0 ? open : 0.f)) {
                int target = gOpenTool == 0 ? -1 : 0;
                if (target >= 0 && bodyKind >= 0 && bodyKind != target && open > 0.005f) { pendingTool = target; gOpenTool = -1; }
                else { pendingTool = -1; gOpenTool = target; }
                gFollow = gOpenTool == 0;
            }
            if (NavButton(1, I_DISTORT, "Distort", gOpenTool == 1 ? open : 0.f)) {
                int target = gOpenTool == 1 ? -1 : 1;
                if (target >= 0 && bodyKind >= 0 && bodyKind != target && open > 0.005f) { pendingTool = target; gOpenTool = -1; }
                else { pendingTool = -1; gOpenTool = target; }
                gFollow = gOpenTool == 1;
            }
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
    static int previousSelected = -2;
    if (previousSelected != gTimelineSelected) {
        sentRotation = gRotT;
        sentOpacity = gSlT[0];
        previousSelected = gTimelineSelected;
    }
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
                Clay_ElementId rotationValueId = CLAY_ID("RotationValue");
                if (ValBoxEditable(rotationValueId, &rotationValue, "\xC2\xB0", 128, 35, 22, 18, 11, -180.f, 180.f, 1)) {
                    gRot = gRotT = rotationValue;
                    sentRotation = rotationValue;
                    PlutoTimelineSetProperty("props.transform.rotation", rotationValue);
                    gPreviewDirty = true;
                }
                if (gValueDragging && gValueDragId == rotationValueId.id) {
                    gRot = gRotT = rotationValue;
                    PlutoTimelineSetPropertyLive("props.transform.rotation", rotationValue);
                    sentRotation = rotationValue;
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
            if (gRotDrag && fabsf(gRotT - sentRotation) > 0.01f) {
                PlutoTimelineSetPropertyLive("props.transform.rotation", gRotT);
                sentRotation = gRotT;
            } else if (!gRotDrag && fabsf(gRotT - sentRotation) > 0.01f) {
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
                Clay_ElementId opacityValueId = CLAY_ID("OpacityValue");
                if (ValBoxEditable(opacityValueId, &opacityValue, "%", 72, 27, 16, 13, 8, 0.f, 100.f, 0)) {
                    gSl[0] = gSlT[0] = opacityValue / 100.f;
                    PlutoTimelineSetProperty("props.transform.opacity", opacityValue);
                    sentOpacity = gSlT[0];
                }
                if (gValueDragging && gValueDragId == opacityValueId.id) {
                    gSl[0] = gSlT[0] = opacityValue / 100.f;
                    PlutoTimelineSetPropertyLive("props.transform.opacity", opacityValue);
                    sentOpacity = gSlT[0];
                }
            }
            Clay_ElementId sid = CLAY_ID("OpacitySlider");
            Bs b = Interact(sid);
            SliderLogic(sid, 0, 8.5f);
            if (fabsf(gSlT[0] - sentOpacity) > 0.002f) {
                if (gSlDrag[0]) PlutoTimelineSetPropertyLive("props.transform.opacity", gSlT[0] * 100.f);
                else PlutoTimelineSetProperty("props.transform.opacity", gSlT[0] * 100.f);
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
    static bool wasDragging[3];
    static int previousSelected = -2;
    CLAY(CLAY_IDI("WheelCol", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId wid = CLAY_IDI("Wheel", i);
        Bs b = Interact(wid);
        WheelLogic(i, wid);
        if (!sentInit || previousSelected != gTimelineSelected) {
            for (int j = 0; j < 3; j++) { sent[j] = gWhT[j]; wasDragging[j] = false; }
            sentInit = true;
            previousSelected = gTimelineSelected;
        }
        bool resetPuck = gWhResetPending[i];
        if ((gWhDrag[i] || wasDragging[i] || resetPuck) &&
            (resetPuck || fabsf(gWhT[i].x - sent[i].x) + fabsf(gWhT[i].y - sent[i].y) > .0015f)) {
            static const char *grade[3] = {"shadows", "tint", "highlights"};
            float hue, amount; WheelInfo(gWhT[i], &hue, &amount);
            char updates[256];
            snprintf(updates, sizeof(updates), "[{\"path\":\"props.colors.grading.%s.hue\",\"value\":%.4f},{\"path\":\"props.colors.grading.%s.amount\",\"value\":%.4f}]",
                     grade[i], hue, grade[i], amount * 100.f);
            if (gWhDrag[i]) PlutoTimelineSetPropertiesLiveJson(updates);
            else PlutoTimelineSetPropertiesJson(updates);
            sent[i] = gWhT[i];
            gWhResetPending[i] = false;
        }
        wasDragging[i] = gWhDrag[i];
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
    static int previousSelected = -2;
    if (!sentInit || previousSelected != gTimelineSelected) {
        memcpy(sent, gSlT, sizeof(sent));
        sentInit = true;
        previousSelected = gTimelineSelected;
    }
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
        if (fabsf(gSlT[gi] - sent[gi]) > .0015f && idx > 0 && idx < 5) {
            char path[128];
            ColorSliderProperty(idx, s, path, sizeof(path));
            double value = ColorSliderBackendValue(idx, s, gSlT[gi]);
            if (gSlDrag[gi]) PlutoTimelineSetPropertyLive(path, value);
            else PlutoTimelineSetProperty(path, value);
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
static const FilterP kFilters[6] = {
    {{236,110,96,255},  {84,66,208,255},  {250,226,140,255}},
    {{255,150,40,255},  {70,88,232,255},  {255,222,120,255}},
    {{158,158,164,255}, {38,38,44,255},   {240,240,244,255}},
    {{232,160,136,255}, {120,98,192,255}, {250,226,150,255}},
    {{60,176,204,255},  {38,48,124,255},  {250,236,170,255}},
    {{128,196,112,255}, {28,86,80,255},   {250,236,160,255}},
};
static const char *kFilterNames[6] = {"Original", "Vivid", "Noir", "Fade", "Warm", "Cool"};
static const char *kFilterIds[6] = {"none", "vivid", "noir", "fade", "warm", "cool"};

static void FilterThumb(int i) {
    Clay_ElementId id = CLAY_IDI("Flt", i);
    Bs b = Interact(id);
    if (b.clicked) {
        gFilter = i;
        gFilterIntensity = gFilterIntensitySent = 1.f;
        PlutoTimelineSetPropertiesJson(TextFormat("[{\"path\":\"props.effects.preset.id\",\"value\":\"%s\"},{\"path\":\"props.effects.preset.intensity\",\"value\":100}]", kFilterIds[i]));
    }
    CLAY(id, {.layout = {COLM, .sizing = {GROW, FIX(78)}, .padding = {7, 7, 6, 6}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}},
              .custom = {.customData = NC(K_FILTER, kFilters[i].a, kFilters[i].b, kFilters[i].sun, 0, b.hover, gFilter == i)}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, GROW}}}) {}
        Tx(kFilterNames[i], 13, C_TEXT, 1);
    }
}

/* ---- video effects (accordion: one effect open at a time) ---- */
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
static int   gFxOpen = -1;
static bool  gVfxOpen = false, gFiltOpen = false;

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
        FxSliderLogic(sid, v, &gFxSlDrag[i][p], 7.f);
        if (fabsf(*v - gFxSent[i][p]) > .0015f) {
            double value = (p == 1 && kFx[i].angle) ? (*v - .5f) * 360.0 : *v * 100.0;
            if (gFxSlDrag[i][p]) PlutoTimelineSetPropertyLive(gFxProp[i][p], value);
            else PlutoTimelineSetProperty(gFxProp[i][p], value);
            gFxSent[i][p] = *v;
        }
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(16)}},
                   .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD, *v,
                                               (bs.hover + (gFxSlDrag[i][p] ? 1.f : 0.f)) * 1.5f, 0)}}) {}
    }
}

static void FxBody(int i) {
    CLAY(CLAY_IDI("FxBody", i), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {14, 12, 12, 12}, .childGap = 9},
                                 .backgroundColor = C_RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(1)}}, .backgroundColor = C_RCARDB}) {}
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

static bool EffectsSectionHeader(Clay_ElementId id, const char *title, int icon, bool open, bool modified, bool connected);

static void ExtraFxRow(int i) {
    Clay_ElementId id = CLAY_IDI("FxExtra", i);
    Bs b = Interact(id);
    if (b.clicked) {
        gExtraFxOn[i] = !gExtraFxOn[i];
        char path[96]; snprintf(path, sizeof(path), "props.effects.%s.enabled", gExtraFxKeys[i]);
        if (gExtraFxOn[i] && strcmp(gExtraFxKeys[i], "blur") == 0) {
            PlutoTimelineSetPropertiesJson("[{\"path\":\"props.effects.blur.enabled\",\"value\":true},{\"path\":\"props.effects.blur.amount\",\"value\":22},{\"path\":\"props.effects.blur.mode\",\"value\":\"gaussian\"}]");
        } else if (gExtraFxOn[i] && strcmp(gExtraFxKeys[i], "grain") == 0) {
            PlutoTimelineSetPropertiesJson("[{\"path\":\"props.effects.grain.enabled\",\"value\":true},{\"path\":\"props.effects.grain.amount\",\"value\":28},{\"path\":\"props.effects.grain.roughness\",\"value\":50},{\"path\":\"props.effects.grain.size\",\"value\":30}]");
        } else if (gExtraFxOn[i] && strcmp(gExtraFxKeys[i], "vignette") == 0) {
            PlutoTimelineSetPropertiesJson("[{\"path\":\"props.effects.vignette.enabled\",\"value\":true},{\"path\":\"props.effects.vignette.amount\",\"value\":50},{\"path\":\"props.effects.vignette.midpoint\",\"value\":50},{\"path\":\"props.effects.vignette.roundness\",\"value\":0},{\"path\":\"props.effects.vignette.feather\",\"value\":65}]");
        } else {
            PlutoTimelineSetBoolProperty(path, gExtraFxOn[i]);
        }
    }

    float openAnim = EaseIO(AnimLin(id.id, gExtraFxOn[i] ? 1.f : 0.f, 0.2f));
    bool down = Clay_PointerOver(id) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float squash = Clampf(Spring(id.id + 7u, down ? 1.f : 0.f, 700.f, 26.f), 0.f, 1.2f);
    int inset = (int)roundf(squash * 4.f);
    int verticalInset = (int)roundf(squash * 1.5f);
    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(48)}, .padding = {inset, inset, verticalInset, verticalInset}}}) {
        CLAY(id, {.layout = {.sizing = {GROW, GROW}, .padding = {16, 14, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
                  .backgroundColor = Mix(Mix(C_RCARD, RGB(33, 34, 38), openAnim), Mix(RGB(36, 37, 43), RGB(40, 41, 47), openAnim), b.hover),
                  .cornerRadius = CLAY_CORNER_RADIUS(18),
                  .border = {.color = Mix(Mix(C_RCARDB, RGB(62, 64, 72), openAnim), RGB(84, 87, 96), b.hover * 0.7f),
                             .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Icon(I_SPARKLE, 22, C_ICON);
            Tx(gExtraFxNames[i], 18, C_TEXT, 0);
            HSPACE
            if (gExtraFxOn[i]) {
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

/* Effect rows use the same 48px accordion card as the Colors panel. */
static bool FxHeader(int i, float open) {
    Clay_ElementId id = CLAY_IDI("FxHdr", i);
    return EffectsSectionHeader(id, kFx[i].name, I_SPARKLE, open > 0.5f, gFxOn[i], true);
}

static float gEffectSectionHeight[3] = {96.f, 286.f, 1005.f};
static float gFxBodyHeight[4] = {180.f, 180.f, 180.f, 180.f};
static bool gTransitionBodyMeasured;

static bool EffectsSectionHeader(Clay_ElementId id, const char *title, int icon, bool open, bool modified, bool connected) {
    Bs b = Interact(id);
    float openAnim = EaseIO(AnimLin(id.id + 0x173u, open ? 1.f : 0.f, 0.2f));
    bool down = Clay_PointerOver(id) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float squash = Clampf(Spring(id.id + 7u, down ? 1.f : 0.f, 700.f, 26.f), 0.f, 1.2f);
    int inset = (int)roundf(squash * 4.f);
    int verticalInset = (int)roundf(squash * 1.5f);
    Clay_Color base = C_RCARD;
    Clay_Color hoverBase = RGB(40, 41, 47);
    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(48)},
                              .padding = {connected ? 0 : inset, connected ? 0 : inset,
                                          connected ? 0 : verticalInset, connected ? 0 : verticalInset}}}) {
        CLAY(id, {.layout = {.sizing = {GROW, GROW}, .padding = {16, 14, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
                  .backgroundColor = Mix(Mix(base, RGB(33, 34, 38), openAnim), hoverBase, b.hover * 0.55f),
                  .cornerRadius = connected ? CLAY_CORNER_RADIUS(0) : CLAY_CORNER_RADIUS(18),
                  .border = {.color = connected ? RGBA(0, 0, 0, 0) :
                                  Mix(C_RCARDB, RGB(62, 64, 72), b.hover * 0.48f),
                             .width = CLAY_BORDER_OUTSIDE(connected ? 0 : 1)}}) {
            Icon(icon, 22, C_ICON);
            Tx(title, 18, C_TEXT, 0);
            HSPACE
            if (modified) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(6), FIX(6)}}, .backgroundColor = C_TEXT,
                              .cornerRadius = CLAY_CORNER_RADIUS(3)}) {}
                GAPX(2)
            }
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(20), FIX(20)}, .childAlignment = AL_C},
                          .cornerRadius = CLAY_CORNER_RADIUS(10),
                          .backgroundColor = RGBA(0, 0, 0, 0),
                          .border = {.color = Mix(RGB(74, 77, 85), RGB(112, 116, 125), b.hover * 0.55f),
                                     .width = CLAY_BORDER_OUTSIDE(1)}}) {
                IconV(I_PLUS, 11, C_DIM, 1.f - openAnim);
            }
        }
    }
    return b.clicked;
}

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

static void FilterIntensityControl(void) {
    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(22)}, .childAlignment = AL_Y}}) {
            Tx("Intensity", 13, C_MUTED, 0);
            HSPACE
            float value = roundf(gFilterIntensity * 100.f);
            if (ValBoxEditable(CLAY_ID("FilterIntensityValue"), &value, "%", 66, 22, 13, 11, 10, 0.f, 100.f, 0)) {
                gFilterIntensity = gFilterIntensitySent = Clampf(value / 100.f, 0.f, 1.f);
                PlutoTimelineSetProperty("props.effects.preset.intensity", gFilterIntensity * 100.0);
            }
        }
        Clay_ElementId id = CLAY_ID("FilterIntensitySlider");
        Bs b = Interact(id);
        static bool wasDragging;
        if (Clay_PointerOver(id) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = id.id;
        gFilterIntensityDrag = false;
        if (gDragId == id.id) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Clay_BoundingBox bb = Clay_GetElementData(id).boundingBox;
                if (bb.width > 14.f) gFilterIntensity = Clampf((GetMousePosition().x - bb.x - 7.f) / (bb.width - 14.f), 0.f, 1.f);
                gFilterIntensityDrag = true;
            } else gDragId = 0;
        }
        bool changed = fabsf(gFilterIntensity - gFilterIntensitySent) > .0015f;
        if (changed) {
            if (gFilterIntensityDrag) PlutoTimelineSetPropertyLive("props.effects.preset.intensity", gFilterIntensity * 100.0);
            else PlutoTimelineSetProperty("props.effects.preset.intensity", gFilterIntensity * 100.0);
            gFilterIntensitySent = gFilterIntensity;
        } else if (wasDragging && !gFilterIntensityDrag) {
            PlutoTimelineSetProperty("props.effects.preset.intensity", gFilterIntensity * 100.0);
        }
        wasDragging = gFilterIntensityDrag;
        CLAY(id, {.layout = {.sizing = {GROW, FIX(16)}},
                  .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD, gFilterIntensity,
                                              (b.hover + (gFilterIntensityDrag ? 1.f : 0.f)) * 1.5f, 0)}}) {}
    }
}

static void EffectsView(void) {
    int from = gTransitionFocusClip >= 0 ? gTransitionFocusClip : gTimelineSelected;
    int next = TransitionNextClip(from);

    CLAY(CLAY_ID("EffectsRoot"), {.layout = {COLM, .sizing = {GROW, FIT},
                                         .padding = {0, 12, 0, 0}, .childGap = 10},
                                  .clip = {.horizontal = true}}) {
        /* Transitions: a measured-height wrapper animates the content just
           like the Colors accordions, with the same 1px border and radius. */
        Clay_ElementId transId = CLAY_ID("TransHdr");
        float transOpen = EaseIO(AnimLin(transId.id, gTransOpen ? 1.f : 0.f, 0.2f));
        float transMeasured = Clay_GetElementData(CLAY_ID("TransBody")).boundingBox.height;
        if (transMeasured > 1.f) {
            gEffectSectionHeight[0] = transMeasured;
            gTransitionBodyMeasured = true;
        } else if (!gTransitionBodyMeasured) {
            gEffectSectionHeight[0] = next < 0 ? 96.f : 265.f;
        }
        float transWrapHeight = roundf(transOpen * (gEffectSectionHeight[0] + 10.f));
        bool transitionModified = strcmp(gTransitionType, "none") != 0;
        if (EffectsSectionHeader(transId, "Transitions", I_BOWTIE, gTransOpen,
                                 transitionModified, false)) gTransOpen = !gTransOpen;
        if (transWrapHeight >= 1.f) {
            CLAY(CLAY_ID("TransWrap"), {.layout = {COLM, .sizing = {GROW, FIX(transWrapHeight)}},
                                        .clip = {.horizontal = true, .vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_ID("TransBody"), {.layout = {COLM, .sizing = {GROW, FIT},
                                                  .padding = {14, 14, 12, 12}, .childGap = 8}, RCARD,
                                             .clip = {.horizontal = true}}) {
                    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(1)}}, .backgroundColor = C_RCARDB}) {}
                    if (next < 0) {
                        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) {
                            TxWrap("Place a second video clip directly after the selected clip, then use the plus at their join.", 14, C_MUTED, 0);
                        }
                    } else {
                        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) {
                            TxWrap(Fmt("%s to %s", gTimelineClips[from].label, gTimelineClips[next].label), 13, C_MUTED, 0);
                        }
                        const char *names[6] = {"None", "Cross dissolve", "Dip to black", "Wipe", "Iris", "Slide"};
                        const char *keys[6] = {"none", "crossfade", "dip", "wipe", "iris", "slide"};
                        for (int row = 0; row < 3; row++) {
                            CLAY(CLAY_IDI("TransitionChoiceRow", row),
                                 {.layout = {.sizing = {GROW, FIX(42)}, .childGap = 5},
                                  .clip = {.horizontal = true}}) {
                                for (int col = 0; col < 2; col++) {
                                    int i = row * 2 + col;
                                    Clay_ElementId cid = CLAY_IDI("TransitionChoice", i);
                                    Bs cb = Interact(cid);
                                    bool selected = strcmp(gTransitionType, keys[i]) == 0;
                                    if (cb.clicked) { strcpy(gTransitionType, keys[i]); CommitTransition(); }
                                    CLAY(cid, {.layout = {.sizing = {GROW, GROW}, .padding = {6, 6, 0, 0},
                                                          .childAlignment = AL_C},
                                              .backgroundColor = BtnBg(selected ? C_SEL : C_CARD,
                                                                        RGB(43, 45, 52), cb),
                                              .cornerRadius = CLAY_CORNER_RADIUS(10),
                                              .border = {.color = Mix(C_CARDB, RGB(80, 83, 91),
                                                                      selected ? .60f : cb.hover * .32f),
                                                         .width = CLAY_BORDER_OUTSIDE(1)}}) {
                                        TxWrap(names[i], 13, selected ? C_TEXT : C_MUTED, 0);
                                    }
                                }
                            }
                        }
                        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(22)}, .childAlignment = AL_Y}}) {
                            Tx("Duration", 13, C_MUTED, 0); HSPACE;
                            float durationValue = gTransitionDuration;
                            if (ValBoxEditable(CLAY_ID("TransitionDurationValue"), &durationValue,
                                               "s", 72, 22, 13, 11, 10, .1f, 2.f, 1)) {
                                gTransitionDuration = durationValue;
                                CommitTransition();
                            }
                        }
                        Clay_ElementId did = CLAY_ID("TransitionDuration");
                        static bool durationDrag;
                        if (Clay_PointerOver(did) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) durationDrag = true;
                        if (durationDrag && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                            Clay_BoundingBox bb = Clay_GetElementData(did).boundingBox;
                            if (bb.width > 1.f)
                                gTransitionDuration = .1f + 1.9f * Clampf((GetMousePosition().x - bb.x) / bb.width, 0.f, 1.f);
                        }
                        if (durationDrag && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                            durationDrag = false;
                            CommitTransition();
                        }
                        CLAY(did, {.layout = {.sizing = {GROW, FIX(16)}},
                                   .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160),
                                                               C_CARD, (gTransitionDuration - .1f) / 1.9f,
                                                               durationDrag ? 1.f : Clay_PointerOver(did), 0)}}) {}
                    }
                }
            }
        }

        /* Filters: six practical presets, no category pills or hidden rows. */
        Clay_ElementId filtersId = CLAY_ID("FiltersHdr");
        float filtersOpen = EaseIO(AnimLin(filtersId.id, gFiltOpen ? 1.f : 0.f, 0.2f));
        float filtersMeasured = Clay_GetElementData(CLAY_ID("FiltBody")).boundingBox.height;
        if (filtersMeasured > 1.f) gEffectSectionHeight[1] = filtersMeasured;
        float filtersWrapHeight = roundf(filtersOpen * (gEffectSectionHeight[1] + 10.f));
        if (EffectsSectionHeader(filtersId, "Filters", I_SPARKLE, gFiltOpen,
                                 gFilter != 0, false)) gFiltOpen = !gFiltOpen;
        if (filtersWrapHeight >= 1.f) {
            CLAY(CLAY_ID("FiltersWrap"), {.layout = {COLM, .sizing = {GROW, FIX(filtersWrapHeight)}},
                                          .clip = {.horizontal = true, .vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_ID("FiltBody"), {.layout = {COLM, .sizing = {GROW, FIT},
                                                 .padding = {12, 12, 10, 10}, .childGap = 10}, RCARD,
                                           .clip = {.horizontal = true}}) {
                    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(22)}, .childAlignment = AL_Y}}) {
                        Tx("Looks", 14, C_TEXT, 0);
                        HSPACE
                        Clay_ElementId rid = CLAY_ID("FiltersReset");
                        Bs rb = Interact(rid);
                        Clay_Color rc = Mix(RGB(140, 142, 150), C_TEXT, rb.hover);
                        CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                            ResetIcon(rid, I_RESET, 14, rc, rb.clicked);
                            Tx("Reset", 13, rc, 0);
                        }
                        if (rb.clicked) {
                            gFilter = 0;
                            gFilterIntensity = gFilterIntensitySent = 1.f;
                            PlutoTimelineSetPropertiesJson("[{\"path\":\"props.effects.preset.id\",\"value\":\"none\"},{\"path\":\"props.effects.preset.intensity\",\"value\":100}]");
                        }
                    }
                    for (int row = 0; row < 2; row++) {
                        CLAY(CLAY_IDI("FltRow", row), {.layout = {.sizing = {GROW, FIX(78)}, .childGap = 8}}) {
                            for (int col = 0; col < 3; col++) FilterThumb(row * 3 + col);
                        }
                    }
                    FilterIntensityControl();
                }
            }
        }

        /* Video Effects: section expansion and each individual effect use
           measured-height animation. Every effect's header and controls share
           one bordered card so the expanded rows stay visually connected. */
        bool anyVideoEffect = false;
        for (int i = 0; i < 4; i++) anyVideoEffect = anyVideoEffect || gFxOn[i];
        for (int i = 0; i < EXTRA_FX_COUNT; i++) anyVideoEffect = anyVideoEffect || gExtraFxOn[i];
        Clay_ElementId vfxId = CLAY_ID("VfxHdr");
        float vfxOpen = EaseIO(AnimLin(vfxId.id, gVfxOpen ? 1.f : 0.f, 0.2f));
        float vfxMeasured = Clay_GetElementData(CLAY_ID("VfxBody")).boundingBox.height;
        if (vfxMeasured > 1.f) gEffectSectionHeight[2] = vfxMeasured;
        float vfxWrapHeight = roundf(vfxOpen * (gEffectSectionHeight[2] + 10.f));
        if (EffectsSectionHeader(vfxId, "Video Effects", I_FILM, gVfxOpen,
                                 anyVideoEffect, false)) gVfxOpen = !gVfxOpen;
        if (vfxWrapHeight >= 1.f) {
            CLAY(CLAY_ID("VfxWrap"), {.layout = {COLM, .sizing = {GROW, FIX(vfxWrapHeight)}},
                                      .clip = {.horizontal = true, .vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_ID("VfxBody"), {.layout = {COLM, .sizing = {GROW, FIT},
                                               .padding = {10, 10, 10, 10}, .childGap = 8}, RCARD,
                                          .clip = {.horizontal = true}}) {
                    for (int i = 0; i < 4; i++) {
                        Clay_ElementId fxId = CLAY_IDI("FxHdr", i);
                        float fxOpen = EaseIO(AnimLin(fxId.id, gFxOpen == i ? 1.f : 0.f, 0.2f));
                        float fxMeasured = Clay_GetElementData(CLAY_IDI("FxBody", i)).boundingBox.height;
                        if (fxMeasured > 1.f) gFxBodyHeight[i] = fxMeasured;
                        float fxWrapHeight = roundf(fxOpen * gFxBodyHeight[i]);
                        CLAY(CLAY_IDI("FxItem", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 0},
                                                       .backgroundColor = C_RCARD,
                                                       .cornerRadius = CLAY_CORNER_RADIUS(18),
                                                       .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)},
                                                       .clip = {.horizontal = true, .vertical = true}}) {
                            if (FxHeader(i, gFxOpen == i ? 1.f : 0.f))
                                gFxOpen = (gFxOpen == i) ? -1 : i;
                            if (fxWrapHeight >= 1.f) {
                                CLAY(CLAY_IDI("FxBodyWrap", i),
                                     {.layout = {COLM, .sizing = {GROW, FIX(fxWrapHeight)}},
                                      .clip = {.horizontal = true, .vertical = true}}) {
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

/* ---------------- integrated title editor ---------------- */

static void PlutoTextAppendCodepoint(int cp) {
    size_t used = strlen(gTextContent);
    if ((cp < 32 && cp != '\n') || cp > 0x10ffff || used + 5 >= sizeof(gTextContent)) return;
    if (cp < 0x80) gTextContent[used++] = (char)cp;
    else if (cp < 0x800) {
        gTextContent[used++] = (char)(0xc0 | (cp >> 6));
        gTextContent[used++] = (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        gTextContent[used++] = (char)(0xe0 | (cp >> 12));
        gTextContent[used++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        gTextContent[used++] = (char)(0x80 | (cp & 0x3f));
    } else {
        gTextContent[used++] = (char)(0xf0 | (cp >> 18));
        gTextContent[used++] = (char)(0x80 | ((cp >> 12) & 0x3f));
        gTextContent[used++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        gTextContent[used++] = (char)(0x80 | (cp & 0x3f));
    }
    gTextContent[used] = 0;
}

static void PlutoTextBackspace(void) {
    size_t n = strlen(gTextContent);
    if (!n) return;
    n--;
    while (n > 0 && (((unsigned char)gTextContent[n] & 0xc0u) == 0x80u)) n--;
    gTextContent[n] = 0;
}

static int PlutoTextBuildLines(Font font, int fontSize, char lines[16][STELLA_TEXT_MAX]) {
    char source[STELLA_TEXT_MAX];
    strncpy(source, gTextContent, sizeof(source) - 1);
    source[sizeof(source) - 1] = 0;
    int count = 0;
    char *lineStart = source;
    while (lineStart && count < 16) {
        char *newline = strchr(lineStart, '\n');
        if (newline) *newline = 0;
        char current[STELLA_TEXT_MAX] = {0};
        char *word = lineStart;
        while (*word && count < 16) {
            while (*word == ' ' || *word == '\t' || *word == '\r') word++;
            if (!*word) break;
            char *wordEnd = word;
            while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\t' && *wordEnd != '\r') wordEnd++;
            char saved = *wordEnd;
            *wordEnd = 0;
            char candidate[STELLA_TEXT_MAX];
            snprintf(candidate, sizeof(candidate), "%s%s%s", current, current[0] ? " " : "", word);
            float width = MeasureTextEx(font, candidate, (float)fontSize, 0.f).x;
            if (current[0] && width > 1640.f) {
                strncpy(lines[count++], current, STELLA_TEXT_MAX - 1);
                lines[count - 1][STELLA_TEXT_MAX - 1] = 0;
                strncpy(current, word, sizeof(current) - 1);
                current[sizeof(current) - 1] = 0;
            } else {
                strncpy(current, candidate, sizeof(current) - 1);
                current[sizeof(current) - 1] = 0;
            }
            *wordEnd = saved;
            word = wordEnd;
        }
        if (count < 16) {
            strncpy(lines[count], current, STELLA_TEXT_MAX - 1);
            lines[count][STELLA_TEXT_MAX - 1] = 0;
            count++;
        }
        lineStart = newline ? newline + 1 : NULL;
    }
    return count > 0 ? count : 1;
}

static bool PlutoTextOutputPath(char *path, size_t capacity) {
#ifdef _WIN32
    char root[MAX_PATH] = {0}, parent[MAX_PATH] = {0}, folder[MAX_PATH] = {0};
    DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", root, (DWORD)sizeof(root));
    if (!length || length >= sizeof(root)) {
        DWORD tempLength = GetTempPathA((DWORD)sizeof(root), root);
        if (!tempLength || tempLength >= sizeof(root)) return false;
        size_t rootLength = strlen(root);
        while (rootLength > 0 && (root[rootLength - 1] == '\\' || root[rootLength - 1] == '/'))
            root[--rootLength] = 0;
    }
    if (snprintf(parent, sizeof(parent), "%s\\Stella", root) >= (int)sizeof(parent) ||
        snprintf(folder, sizeof(folder), "%s\\Titles", parent) >= (int)sizeof(folder)) return false;
    CreateDirectoryA(parent, NULL);
    CreateDirectoryA(folder, NULL);
    /* GetTempFileNameA supplies a name unique across app restarts, so a new
       title can never overwrite a PNG still referenced by an older project. */
    char temporary[MAX_PATH];
    if (!GetTempFileNameA(folder, "stl", 0, temporary)) return false;
    DeleteFileA(temporary); /* ExportImage creates the final PNG. */
    char *extension = strrchr(temporary, '.');
    if (!extension) return false;
    strcpy(extension, ".png");
    int n = snprintf(path, capacity, "%s", temporary);
    return n > 0 && (size_t)n < capacity;
#else
    unsigned long long serial = (unsigned long long)gClipSerial++;
    int n = snprintf(path, capacity, "Stella-title-%llu.png", serial);
    return n > 0 && (size_t)n < capacity;
#endif
}

static bool PlutoTextWriteImage(char *outPath, size_t pathCapacity) {
    if (!gTextContent[0]) return false;
    if (!PlutoTextOutputPath(outPath, pathCapacity)) return false;
    const int width = 1920, height = 1080;
    int fontSize = (int)Clampf((float)gTextFontSize, 24.f, 180.f);
    Font font = FontPx(gTextBold ? 2 : 0, fontSize);
    char lines[16][STELLA_TEXT_MAX] = {{0}};
    int lineCount = PlutoTextBuildLines(font, fontSize, lines);
    float lineHeight = (float)fontSize * 1.18f;
    float blockHeight = lineCount * lineHeight;
    float anchorX = Clampf(gTextPosX, .03f, .97f) * (float)width;
    float anchorY = Clampf(gTextPosY, .04f, .96f) * (float)height;
    float y = Clampf(anchorY - blockHeight * .5f, 8.f, (float)height - blockHeight - 8.f);
    float maxWidth = 0.f;
    for (int i = 0; i < lineCount; i++) {
        float w = MeasureTextEx(font, lines[i], (float)fontSize, 0.f).x;
        if (w > maxWidth) maxWidth = w;
    }
    float x = anchorX - (gTextAlign == 0 ? 0.f : gTextAlign == 1 ? maxWidth * .5f : maxWidth);
    x = Clampf(x, 8.f, (float)width - maxWidth - 8.f);

    Image image = GenImageColor(width, height, (Color){0, 0, 0, 0});
    if (!image.data) return false;
    if (gTextBackground) {
        Rectangle plate = {x - 36.f, y - 20.f, maxWidth + 72.f, blockHeight + 40.f};
        if (plate.x < 0.f) plate.x = 0.f;
        if (plate.y < 0.f) plate.y = 0.f;
        if (plate.x + plate.width > width) plate.width = width - plate.x;
        if (plate.y + plate.height > height) plate.height = height - plate.y;
        ImageDrawRectangleRec(&image, plate, gTextAppearance[3]);
    }
    Color ink = gTextAppearance[0];
    for (int i = 0; i < lineCount; i++) {
        float textWidth = MeasureTextEx(font, lines[i], (float)fontSize, 0.f).x;
        float drawX = gTextAlign == 0 ? x : gTextAlign == 1 ? anchorX - textWidth * .5f : anchorX - textWidth;
        drawX = Clampf(drawX, 8.f, (float)width - textWidth - 8.f);
        Vector2 position = {drawX, y + (float)i * lineHeight};
        if (gTextShadow) {
            Vector2 shadow = {position.x + 5.f, position.y + 7.f};
            ImageDrawTextEx(&image, font, lines[i], shadow, (float)fontSize, 0.f, gTextAppearance[2]);
        }
        if (gTextOutline) {
            static const int offsets[8][2] = {{-2,0},{2,0},{0,-2},{0,2},{-2,-2},{2,-2},{-2,2},{2,2}};
            for (int k = 0; k < 8; k++) {
                Vector2 edge = {position.x + (float)offsets[k][0], position.y + (float)offsets[k][1]};
                ImageDrawTextEx(&image, font, lines[i], edge, (float)fontSize, 0.f, gTextAppearance[1]);
            }
        }
        ImageDrawTextEx(&image, font, lines[i], position, (float)fontSize, 0.f, ink);
    }
    bool okay = ExportImage(image, outPath);
    UnloadImage(image);
    return okay;
}

static bool PlutoTextBuildPropsJson(char *out, size_t capacity) {
    char escaped[STELLA_TEXT_MAX * 6 + 16];
    PlutoJsonEscape(gTextContent, escaped, sizeof(escaped));
    int n = snprintf(out, capacity,
        "{\"isTitle\":true,\"content\":\"%s\",\"fontSize\":%d,\"color\":%d,\"align\":%d,\"bold\":%s,\"outline\":%s,\"shadow\":%s,\"background\":%s,\"x\":%.5f,\"y\":%.5f,"
        "\"inkR\":%u,\"inkG\":%u,\"inkB\":%u,\"inkA\":%u,"
        "\"outlineR\":%u,\"outlineG\":%u,\"outlineB\":%u,\"outlineA\":%u,"
        "\"shadowR\":%u,\"shadowG\":%u,\"shadowB\":%u,\"shadowA\":%u,"
        "\"backgroundR\":%u,\"backgroundG\":%u,\"backgroundB\":%u,\"backgroundA\":%u}",
        escaped, gTextFontSize, gTextColorIndex, gTextAlign,
        gTextBold ? "true" : "false", gTextOutline ? "true" : "false",
        gTextShadow ? "true" : "false", gTextBackground ? "true" : "false",
        (double)gTextPosX, (double)gTextPosY,
        (unsigned)gTextAppearance[0].r, (unsigned)gTextAppearance[0].g,
        (unsigned)gTextAppearance[0].b, (unsigned)gTextAppearance[0].a,
        (unsigned)gTextAppearance[1].r, (unsigned)gTextAppearance[1].g,
        (unsigned)gTextAppearance[1].b, (unsigned)gTextAppearance[1].a,
        (unsigned)gTextAppearance[2].r, (unsigned)gTextAppearance[2].g,
        (unsigned)gTextAppearance[2].b, (unsigned)gTextAppearance[2].a,
        (unsigned)gTextAppearance[3].r, (unsigned)gTextAppearance[3].g,
        (unsigned)gTextAppearance[3].b, (unsigned)gTextAppearance[3].a);
    return n > 0 && (size_t)n < capacity;
}

static bool PlutoTextApply(bool forceNew) {
    if (!gTextContent[0]) {
        strcpy(gBackendMessage, "Enter some text first");
        return false;
    }
    char path[1024], props[4096];
    if (!PlutoTextBuildPropsJson(props, sizeof(props)) || !PlutoTextWriteImage(path, sizeof(path))) {
        strcpy(gBackendMessage, "Could not create the title image");
        return false;
    }
    bool update = !forceNew && gTextTitleSelected && gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount;
    bool okay;
    if (update) okay = PlutoTimelineUpdateTitleClip(gTimelineClips[gTimelineSelected].id, path, props);
    else okay = PlutoTimelineAddTitleClip(path, props, gPlayhead, gTextDuration);
    if (okay) {
        gTextInputFocused = false;
        gRightTab = 3;
        strcpy(gBackendMessage, update ? "Title updated" : "Title added to the timeline");
    }
    return okay;
}

static void PlutoTextPlaceAt(float xNormalized, float yNormalized) {
    gTextPosX = Clampf(xNormalized, .03f, .97f);
    gTextPosY = Clampf(yNormalized, .04f, .96f);
    if (!gTextContent[0]) strcpy(gTextContent, "Text");
    gTextInputFocused = false;
    PlutoTextApply(true);
}

static void TextWheelLogic(int index, Clay_ElementId id) {
    static double lastClickTime[4];
    static Vector2 pressPosition[4];
    static bool pressMoved[4];
    if (index < 0 || index >= 4) return;
    Clay_BoundingBox bb = Clay_GetElementData(id).boundingBox;
    Vector2 mouse = GetMousePosition();
    float radius = fminf(bb.width, bb.height) * .5f;
    Vector2 center = {bb.x + bb.width * .5f, bb.y + bb.height * .5f};
    float dx = mouse.x - center.x, dy = mouse.y - center.y;
    bool over = radius > 1.f && Clay_PointerOver(id) && hypotf(dx, dy) <= radius + 6.f;
    if (over) {
        gCross = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            double now = GetTime();
            if (lastClickTime[index] > 0.0 && now - lastClickTime[index] < .38 && !pressMoved[index]) {
                gTextWheelPuckT[index] = (Vector2){0.f, 0.f};
                PlutoTextWheelApplyPosition(index, (Vector2){0.f, 0.f});
                lastClickTime[index] = 0.0;
                gDragTextWheel = -1;
            } else {
                gDragTextWheel = index;
                pressPosition[index] = mouse;
                pressMoved[index] = false;
                lastClickTime[index] = now;
            }
        }
    }
    gTextWheelDrag[index] = false;
    if (gDragTextWheel == index) {
        gCross = true;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && radius > 1.f) {
            if (fabsf(mouse.x - pressPosition[index].x) + fabsf(mouse.y - pressPosition[index].y) > 2.5f)
                pressMoved[index] = true;
            float px = dx / (radius - 1.f), py = dy / (radius - 1.f);
            float length = hypotf(px, py);
            if (length > .92f) { px *= .92f / length; py *= .92f / length; }
            gTextWheelPuckT[index] = (Vector2){px, py};
            PlutoTextWheelApplyPosition(index, gTextWheelPuckT[index]);
            gTextWheelDrag[index] = true;
        } else {
            if (pressMoved[index]) lastClickTime[index] = 0.0;
            gDragTextWheel = -1;
        }
    }
}

static void TextWheelItem(int index, const char *label) {
    CLAY(CLAY_IDI("TextWheelCol", index), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6,
                                                        .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId id = CLAY_IDI("TextWheel", index);
        Bs b = Interact(id);
        TextWheelLogic(index, id);
        Vector2 puck = gTextWheelPuck[index];
        Color color = gTextAppearance[index];
        CLAY(id, {.layout = {.sizing = {FIX(96), FIX(96)}},
                  .custom = {.customData = NC(K_WHEEL, C_CARD,
                      (Clay_Color){puck.x, puck.y, 0, 0},
                      (Clay_Color){color.r, color.g, color.b, color.a},
                      1.f, fmaxf(b.hover * .6f, gTextWheelDrag[index] ? 1.f : 0.f), 0)}}) {}
        Tx(label, 14.5f, C_TEXT, 1);
    }
}

/* Light primary button, same family as the Import button / selected pills. */
static bool TextActionButton(const char *idName, const char *label, bool primary) {
    Clay_ElementId id = TextDynamicElementId(idName);
    Bs b = Interact(id);
    Clay_Color fill = primary ? Mix(RGB(226, 228, 234), RGB(246, 247, 250), b.hover) : BtnBg(C_RFIELD, RGB(46, 47, 54), b);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(44)}, .padding = {14, 14, 0, 0}, .childAlignment = AL_C},
              .backgroundColor = fill, .cornerRadius = CLAY_CORNER_RADIUS(14),
              .border = {.color = primary ? RGBA(0, 0, 0, 0) : Mix(C_RCARDB, RGB(80, 83, 92), b.hover * .7f),
                         .width = CLAY_BORDER_OUTSIDE(primary ? 0 : 1)}}) {
        Tx(label, 16, primary ? RGB(22, 23, 26) : C_TEXT, 1);
    }
    return b.clicked;
}

/* Toggle / segmented button: same field surface as the number fields. */
static bool TextChoiceButton(Clay_ElementId id, const char *label, bool on, bool dot) {
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(36)}, .padding = {10, 10, 0, 0}, .childGap = 7, .childAlignment = AL_C},
              .backgroundColor = on ? RGB(52, 54, 62) : BtnBg(C_RFIELD, RGB(46, 47, 54), b),
              .cornerRadius = CLAY_CORNER_RADIUS(11),
              .border = {.color = Mix(on ? RGB(92, 96, 106) : C_RCARDB, RGB(84, 87, 96), b.hover * .5f), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        if (dot)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(6), FIX(6)}},
                          .backgroundColor = on ? C_TEXT : RGB(74, 77, 85), .cornerRadius = CLAY_CORNER_RADIUS(3)}) {}
        Tx(label, 14.5f, on ? C_TEXT : C_DIM, 0);
    }
    return b.clicked;
}

static void TextStyleToggle(const char *idName, const char *label, bool *value) {
    if (TextChoiceButton(TextDynamicElementId(idName), label, *value, true)) *value = !*value;
}

/* X / Y field for the text position. Stored 0..1 across the frame, shown as 0..100 %.
   Same look and behaviour as NumField (scrub, double-click to type, steppers). */
static void TextPosField(int n, const char *lab, float *norm, float lo, float hi) {
    Clay_ElementId id = CLAY_IDI("TxtPosF", n);
    Clay_ElementId scrubId = CLAY_IDI("TxtPosScrub", n);
    Bs b = Interact(id);
    float pct = *norm * 100.f;
    ValueFieldLogicEx(scrubId, &pct, .3f, false);
    pct = fmaxf(lo * 100.f, fminf(hi * 100.f, pct));
    *norm = pct / 100.f;
    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(39)}, .childGap = 7, .childAlignment = AL_Y}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(18), FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { Tx(lab, 19, C_ICON, 0); }
        CLAY(id, {.layout = {.sizing = {GROW, FIX(39)}, .padding = {10, 5, 0, 0}, .childGap = 6, .childAlignment = AL_Y},
                  .backgroundColor = Mix(C_RFIELD, RGB(46, 47, 54), b.hover * 0.45f), .cornerRadius = CLAY_CORNER_RADIUS(11),
                  .border = {.color = Mix(C_RFIELD, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            CLAY(scrubId, {.layout = {.sizing = {GROW, GROW}, .childGap = 4, .childAlignment = AL_Y}}) {
                HSPACE
                Tx(gEditValueId == scrubId.id ? Fmt("%s|", gEditValueText) : Fmt("%d", (int)roundf(pct)), 22, C_TEXT, 0);
                Tx("%", 17, C_ICON, 0);
            }
            GAPX(3)
            CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}}}) {
                if (Arrow(CLAY_IDI("TxtPosU", n), I_CHEV_UP)) *norm = fminf(hi, *norm + .01f);
                if (Arrow(CLAY_IDI("TxtPosD", n), I_CHEV_DN)) *norm = fmaxf(lo, *norm - .01f);
            }
        }
    }
}

/* 3 x 3 dot grid: snaps the text to a corner / edge / centre of the frame. */
static void TextPosGrid(void) {
    static const float xs[3] = {.1f, .5f, .9f};
    static const float ys[3] = {.12f, .5f, .88f};
    CLAY(CLAY_ID("TxtPosGrid"), {.layout = {COLM, .sizing = {FIX(88), FIX(84)}, .padding = CLAY_PADDING_ALL(5)},
                                 .backgroundColor = C_RFIELD, .cornerRadius = CLAY_CORNER_RADIUS(14),
                                 .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        for (int row = 0; row < 3; row++) {
            CLAY(CLAY_IDI("TxtPosRow", row), {.layout = {.sizing = {GROW, GROW}}}) {
                for (int col = 0; col < 3; col++) {
                    int i = row * 3 + col;
                    Clay_ElementId cid = CLAY_IDI("TxtPosCell", i);
                    Bs cb = Interact(cid);
                    if (cb.clicked) { gTextPosX = xs[col]; gTextPosY = ys[row]; }
                    bool sel = fabsf(gTextPosX - xs[col]) < .02f && fabsf(gTextPosY - ys[row]) < .02f;
                    float dot = sel ? 9.f : 6.f + 2.f * cb.hover;
                    CLAY(cid, {.layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C}}) {
                        CLAY_AUTO_ID({.layout = {.sizing = {FIX(dot), FIX(dot)}},
                                      .backgroundColor = sel ? C_TEXT : Mix(RGB(88, 91, 100), RGB(150, 152, 160), cb.hover),
                                      .cornerRadius = CLAY_CORNER_RADIUS(dot * .5f)}) {}
                    }
                }
            }
        }
    }
}

static void TextView(void) {
    CLAY(CLAY_ID("TextCol"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10}}) {

        /* ---- Content ---- */
        CLAY(CLAY_ID("TextContentCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {13, 13, 12, 13}, .childGap = 10}, RCARD}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(24)}, .padding = {3, 0, 0, 0}, .childGap = 10, .childAlignment = AL_Y}}) {
                Icon(I_TEXT, 20, C_ICON);
                Tx("Content", 19, C_TEXT, 1);
                HSPACE
                if (gTextTitleSelected) Tx("Editing title", 13, C_MUTED, 0);
            }
            Clay_ElementId field = CLAY_ID("TextContentField");
            bool over = Clay_PointerOver(field);
            Bs fb = Interact(field);
            if (fb.clicked) gTextInputFocused = true;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !over && gTextInputFocused) gTextInputFocused = false;
            if (gTextInputFocused) {
                if (IsKeyPressed(KEY_ESCAPE)) gTextInputFocused = false;
                if (IsKeyPressed(KEY_BACKSPACE)) PlutoTextBackspace();
                if (IsKeyPressed(KEY_ENTER)) PlutoTextAppendCodepoint('\n');
                int cp;
                while ((cp = GetCharPressed()) != 0) PlutoTextAppendCodepoint(cp);
            }
            CLAY(field, {.layout = {COLM, .sizing = {GROW, FIX(104)}, .padding = {12, 12, 10, 10}},
                         .backgroundColor = Mix(C_RFIELD, RGB(46, 47, 54), gTextInputFocused ? .45f : fb.hover * .3f),
                         .cornerRadius = CLAY_CORNER_RADIUS(11),
                         .border = {.color = gTextInputFocused ? RGB(104, 108, 120) : Mix(C_RFIELD, RGB(76, 79, 88), fb.hover),
                                    .width = CLAY_BORDER_OUTSIDE(1)}}) {
                TxWrap(gTextContent[0] ? gTextContent : "Click here and type your title", 16, gTextContent[0] ? C_TEXT : C_MUTED, 0);
            }
            if (TextActionButton("TextApply", gTextTitleSelected ? "Update selected title" : "Add title to timeline", true))
                PlutoTextApply(false);
        }

        /* ---- Position (where the title sits in the frame) ---- */
        CLAY(CLAY_ID("TextPosCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {13, 13, 12, 13}, .childGap = 10}, RCARD}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(24)}, .padding = {3, 0, 0, 0}, .childGap = 10, .childAlignment = AL_Y}}) {
                Icon(I_POS, 20, C_ICON);
                Tx("Position", 19, C_TEXT, 1);
                HSPACE
                if (IconBtn(CLAY_ID("TxtPosReset"), I_RESET, 24, 13, RGB(104, 105, 112), false, 6)) {
                    gTextPosX = .5f; gTextPosY = .78f;
                }
            }
            if (gRightW <= RIGHT_MIN + 28.f) {
                /* The narrow inspector stacks the position picker and fields
                   so the percentage controls never extend beyond the panel. */
                CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 9, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
                    TextPosGrid();
                    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 8}}) {
                        TextPosField(0, "X", &gTextPosX, .03f, .97f);
                        TextPosField(1, "Y", &gTextPosY, .04f, .96f);
                    }
                }
            } else {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 12, .childAlignment = AL_Y}}) {
                    TextPosGrid();
                    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10}}) {
                        TextPosField(0, "X", &gTextPosX, .03f, .97f);
                        TextPosField(1, "Y", &gTextPosY, .04f, .96f);
                    }
                }
            }
        }

        /* ---- Font ---- */
        CLAY(CLAY_ID("TextFontCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {13, 13, 12, 13}, .childGap = 10}, RCARD}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(27)}, .childAlignment = AL_Y}}) {
                Tx("Font size", 18, C_TEXT, 0);
                HSPACE
                float sizeValue = (float)gTextFontSize;
                if (ValBoxEditable(CLAY_ID("TextFontSize"), &sizeValue, "px", 82, 27, 16, 13, 8, 24.f, 180.f, 0))
                    gTextFontSize = (int)roundf(sizeValue);
            }
            {
                static bool sizeDrag;
                Clay_ElementId sid = CLAY_ID("TextSizeSlider");
                Bs sb = Interact(sid);
                float t = ((float)gTextFontSize - 24.f) / 156.f;
                float before = t;
                FxSliderLogic(sid, &t, &sizeDrag, 7.f);
                if (fabsf(t - before) > .0001f) gTextFontSize = (int)roundf(24.f + t * 156.f);
                CLAY(sid, {.layout = {.sizing = {GROW, FIX(16)}},
                           .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD,
                                                       ((float)gTextFontSize - 24.f) / 156.f,
                                                       (sb.hover + (sizeDrag ? 1.f : 0.f)) * 1.5f, 0)}}) {}
            }
            Tx("Alignment", 16, C_DIM, 0);
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 6}}) {
                static const char *alignNames[3] = {"Left", "Center", "Right"};
                for (int i = 0; i < 3; i++)
                    if (TextChoiceButton(CLAY_IDI("TextAlign", i), alignNames[i], gTextAlign == i, false)) gTextAlign = i;
            }
        }

        /* ---- Appearance ---- */
        CLAY(CLAY_ID("TextLookCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {13, 13, 12, 13}, .childGap = 10}, RCARD}) {
            Tx("Color", 18, C_TEXT, 0);
            for (int row = 0; row < 2; row++) {
                CLAY(CLAY_IDI("TextColorRow", row), {.layout = {.sizing = {GROW, FIT}, .childGap = 8}}) {
                    TextWheelItem(row * 2, row == 0 ? "Text color" : "Shadow color");
                    TextWheelItem(row * 2 + 1, row == 0 ? "Outline color" : "Background color");
                }
            }
            Tx("Double-click a wheel puck to reset", 12.5f, C_MUTED, 0);
            GAPY(2)
            Tx("Style", 18, C_TEXT, 0);
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 6}}) {
                TextStyleToggle("TextBold", "Bold", &gTextBold);
                TextStyleToggle("TextOutline", "Outline", &gTextOutline);
            }
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 6}}) {
                TextStyleToggle("TextShadow", "Shadow", &gTextShadow);
                TextStyleToggle("TextBackground", "Background", &gTextBackground);
            }
        }
    }
}

/* Tab switch: the new page slides in horizontally from the side it sits on in the tab bar
   (+1 = from the right, -1 = from the left) and decelerates into place. No fade. */
#define TAB_SLIDE_SECS 0.20f   /* total duration: quick, not sluggish */
#define TAB_SLIDE_PX   36.f    /* starting horizontal offset */
static int gTabDir = 1;

/* scrolling for the inspector body (wheel + smooth follow + scrollbar thumb) */
static void UpdateInspScroll(void) {
    Clay_BoundingBox bb = Clay_GetElementData(CLAY_ID("InspBody")).boundingBox;
    Clay_BoundingBox cb = Clay_GetElementData(CLAY_ID("InspContent")).boundingBox;
    gInspViewH = bb.height;
    /* Clamp dt so the heavy first frame of a freshly built tab can't swallow the animation. */
    gTabT = fminf(1.f, gTabT + fminf(gDt, 1.f / 30.f) / TAB_SLIDE_SECS);
    {   /* ease-out quartic: fast start, long soft landing */
        float u = 1.f - gTabT;
        gTabE = 1.f - u * u * u * u;
    }
    gFadeA = 0.f;   /* the old cross-fade overlay is no longer used */
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
    bool fitBody = false;
    CLAY(CLAY_ID("Right"), {.layout = {COLM, .sizing = {CLAY_SIZING_FIXED(gRightW), GROW}, .childGap = 10}}) {
        /* tab bar: 54 tall = 6 pad + 42 track (4 pad + 34 pill + 4 pad) + 6 pad */
        CLAY(CLAY_ID("InspTabs"), {.layout = {.sizing = {GROW, FIX(54)}, .padding = CLAY_PADDING_ALL(6)}, RPANEL_R(18)}) {
            CLAY(CLAY_ID("InspSeg"), {.layout = {.sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(4), .childGap = 4},
                                      .cornerRadius = CLAY_CORNER_RADIUS(14),
                                      .custom = {.customData = NC(K_PILL, C_CARD, C_CARD, C_CARD, pill, 0, 0)}}) {
                for (int i = 0; i < 4; i++)
                    if (TabBtn(CLAY_IDI("InspTab", i), tabs[i], gRightTab == i, 15.f, 11, true)) {
                        if (gRightTab != i) { gTextInputFocused = false; gTabDir = (i > gRightTab) ? 1 : -1; gRightTab = i; gScroll = gScrollT = 0.f; gFollow = false; gTabT = 0.f; gTabE = 0.f; }
                    }
            }
        }
        CLAY(CLAY_ID("InspBody"), {.layout = {.sizing = {GROW, fitBody ? FIT : GROW}}, RPANEL,
                                   .clip = {.horizontal = true, .vertical = true, .childOffset = {gTabDir * (1.f - gTabE) * TAB_SLIDE_PX, -gScroll}}}) {
            CLAY(CLAY_ID("InspContent"), {.layout = {COLM, .sizing = {GROW, fitBody ? FIT : CLAY_SIZING_FIT(gInspViewH)}, .padding = {13, 13, 15, 13}, .childGap = 10}, .clip = {.horizontal = true}}) {
                if (gTimelineSelected < 0 && gRightTab != 3) {
                    CLAY(CLAY_ID("NoClip"), {.layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C}}) {
                        CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
                            Icon(I_FILM, 26, C_MUTED);
                            Tx("Nothing to adjust", 17, C_TEXT, 1);
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

