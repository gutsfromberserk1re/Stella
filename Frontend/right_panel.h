/* Cache auto-fit metrics separately from the raster. Keep these declarations
   above PlutoTextWriteImage because this header is compiled as C. */
static uint64_t gTextFitCacheSignature;
static int gTextFitCacheSize;

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

/* Effects with editable preview/export parameters. Expansion and enabled
   state are intentionally independent: the card header opens/closes controls;
   the Enabled switch inside the card body changes whether the effect is applied. */
#define VIDEO_FX_COUNT 2
static float gFxV[VIDEO_FX_COUNT][3] = {{.3f, .5f, .5f}, {.3f, .5f, .5f}};
static float gFxSent[VIDEO_FX_COUNT][3] = {{.3f, .5f, .5f}, {.3f, .5f, .5f}};
static const char *gFxProp[VIDEO_FX_COUNT][3] = {
    {"props.effects.chromatic.amount", "props.effects.chromatic.angle", "props.effects.chromatic.offset"},
    {"props.effects.pixelate.size", "props.effects.pixelate.blend", NULL}
};
static const int gFxParamCount[VIDEO_FX_COUNT] = {3, 2};
static const char *gFxToggle[VIDEO_FX_COUNT] = {
    "props.effects.chromatic.enabled", "props.effects.pixelate.enabled"
};
static bool gFxOn[VIDEO_FX_COUNT];
static bool gFxSlDrag[VIDEO_FX_COUNT][3];
static int gFxOpen = -1;
static bool gVfxOpen = false, gFiltOpen = false;
/* Accordion expansion is view state, not clip-property state. Remember the
   selected clip so a property refresh does not close its currently open card. */
static char gEffectAccordionClipId[64] = {0};
static const char *gExtraFxNames[] = {"Gaussian Blur", "Grain", "Sharpen", "Scanlines", "Camera Shake", "Film Burn", "Halftone", "Posterize", "Light Leak", "Mirror", "Sine Waves"};
static const char *gExtraFxKeys[] = {"blur", "grain", "sharpen", "scanlines", "shake", "filmBurn", "halftone", "posterize", "lightLeak", "mirror", "sineWaves"};
#define EXTRA_FX_COUNT 11
typedef struct { const char *label, *field, *suffix; float min, max, initial; } ExtraFxParamDef;
static const ExtraFxParamDef gExtraFxParams[EXTRA_FX_COUNT][4] = {
    {{"Amount", "amount", "%", 0.f, 100.f, 25.f}},
    {{"Amount", "amount", "", 0.f, 100.f, 35.f}, {"Size", "size", "", 1.f, 100.f, 30.f}, {"Roughness", "roughness", "", 0.f, 100.f, 50.f}},
    {{"Amount", "amount", "%", 0.f, 100.f, 45.f}, {"Radius", "radius", "px", 1.f, 23.f, 5.f}, {"Threshold", "threshold", "%", 0.f, 100.f, 10.f}},
    {{"Spacing", "spacing", "px", 1.f, 20.f, 4.f}, {"Amount", "amount", "%", 0.f, 100.f, 32.f}},
    {{"Amount", "amount", "%", 0.f, 100.f, 18.f}, {"Frequency", "frequency", "Hz", .5f, 30.f, 8.f}, {"Smoothness", "smoothness", "%", 0.f, 100.f, 35.f}},
    {{"Amount", "amount", "%", 0.f, 100.f, 42.f}, {"Warmth", "warmth", "%", 0.f, 100.f, 70.f}, {"Edge", "edge", "%", 0.f, 100.f, 38.f}, {"Flicker", "flicker", "%", 0.f, 100.f, 12.f}},
    {{"Size", "size", "px", 1.f, 24.f, 5.f}, {"Angle", "angle", "°", -180.f, 180.f, 45.f}, {"Contrast", "contrast", "%", 0.f, 100.f, 48.f}},
    {{"Levels", "levels", "", 2.f, 16.f, 6.f}, {"Blend", "blend", "%", 0.f, 100.f, 100.f}},
    {{"Amount", "amount", "%", 0.f, 100.f, 40.f}, {"Warmth", "warmth", "%", 0.f, 100.f, 70.f}, {"Position", "position", "%", 0.f, 100.f, 35.f}},
    {{0}},
    {{"Amount", "amount", "px", 0.f, 100.f, 12.f}, {"Frequency", "frequency", "", 1.f, 24.f, 3.5f}, {"Speed", "speed", "Hz", 0.f, 10.f, 1.2f}}
};
static const int gExtraFxParamCount[EXTRA_FX_COUNT] = {1, 3, 3, 2, 3, 4, 3, 2, 3, 0, 3};
static float gExtraFxV[EXTRA_FX_COUNT][4];
static float gExtraFxSent[EXTRA_FX_COUNT][4];
static bool gExtraFxSlDrag[EXTRA_FX_COUNT][4];
static bool gExtraFxOn[EXTRA_FX_COUNT];
static int gExtraFxOpen = -1;
static float gExtraFxBodyHeight[EXTRA_FX_COUNT] = {180.f, 205.f, 205.f, 155.f, 205.f, 250.f, 205.f, 155.f, 205.f, 88.f, 205.f};
static int gBlurMode; /* 0 Gaussian, 1 Motion, 2 Zoom */
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
    selected.a = gTextAppearance[index].a;
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
    char selectedEffectClipId[64] = {0};
    if (gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount)
        snprintf(selectedEffectClipId, sizeof(selectedEffectClipId), "%s", gTimelineClips[gTimelineSelected].id);
    if (strcmp(gEffectAccordionClipId, selectedEffectClipId) != 0) {
        /* A genuinely different clip gets a fresh inspector accordion state;
           same-clip refreshes after toggles/slider commits keep it expanded. */
        gExtraFxOpen = -1;
        gFxOpen = -1;
        snprintf(gEffectAccordionClipId, sizeof(gEffectAccordionClipId), "%s", selectedEffectClipId);
    }
    gTextInputFocused = false;
    gTextFocusStartedAt = 0.0;
    gTextOutputPath[0] = 0;
    gTextHasCroppedAsset = false;
    gTextRenderedSignature = 0;
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
    static const char *effectNames[2] = {"chromatic", "pixelate"};
    static const char *audioPaths[4] = {"props.audio.volume", "props.audio.pan", "props.audio.bass", "props.audio.treble"};

    gNum[0] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.position.x", 0.f);
    gNum[1] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.position.y", 0.f);
    gNum[2] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.scale.w", 100.f);
    gNum[3] = PlutoReadNumber(clipBegin, clipEnd, "props.transform.scale.h", 100.f);
    gRot = gRotT = PlutoReadNumber(clipBegin, clipEnd, "props.transform.rotation", 0.f);
    gSpeed = fmaxf(0.25f, fminf(10.0f, PlutoReadNumber(clipBegin, clipEnd, "props.transform.speed", 1.0f)));
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

    for (int i = 0; i < 2; i++) {
        float defaults[2][3] = {{.3f, .5f, .5f}, {.3f, .5f, .5f}};
        bool enabled = false;
        char path[128];
        snprintf(path, sizeof(path), "props.effects.%s.enabled", effectNames[i]);
        PlutoJsonBoolPath(clipBegin, clipEnd, path, &enabled);
        gFxOn[i] = enabled;
        snprintf(path, sizeof(path), "props.effects.%s.amount", effectNames[i]);
        if (i == 0) defaults[i][0] = PlutoReadNumber(clipBegin, clipEnd, path, defaults[i][0] * 100.f) / 100.f;
        else {
            snprintf(path, sizeof(path), "props.effects.%s.size", effectNames[i]);
            defaults[i][0] = PlutoReadNumber(clipBegin, clipEnd, path, defaults[i][0] * 100.f) / 100.f;
        }
        snprintf(path, sizeof(path), "props.effects.%s.%s", effectNames[i], i == 0 ? "angle" : "blend");
        float second = PlutoReadNumber(clipBegin, clipEnd, path, i == 0 ? 0.f : defaults[i][1] * 100.f);
        defaults[i][1] = i == 0 ? second / 360.f + .5f : second / 100.f;
        for (int p = 0; p < gFxParamCount[i]; p++) gFxV[i][p] = gFxSent[i][p] = fmaxf(0.f, fminf(1.f, defaults[i][p]));
        if (i == 0) {
            float offset = PlutoReadNumber(clipBegin, clipEnd, "props.effects.chromatic.offset", 0.f);
            gFxV[i][2] = gFxSent[i][2] = fmaxf(0.f, fminf(1.f, .5f + offset / 60.f));
        }
    }
    char blurMode[24] = {0};
    gBlurMode = 0;
    if (PlutoJsonStringPath(clipBegin, clipEnd, "props.effects.blur.mode", blurMode, sizeof(blurMode))) {
        if (strcmp(blurMode, "motion") == 0) gBlurMode = 1;
        else if (strcmp(blurMode, "zoom") == 0) gBlurMode = 2;
    }
    for (int i = 0; i < EXTRA_FX_COUNT; i++) {
        char path[128]; snprintf(path, sizeof(path), "props.effects.%s.enabled", gExtraFxKeys[i]);
        gExtraFxOn[i] = false; PlutoJsonBoolPath(clipBegin, clipEnd, path, &gExtraFxOn[i]);
        for (int p = 0; p < gExtraFxParamCount[i]; p++) {
            char paramPath[160];
            snprintf(paramPath, sizeof(paramPath), "props.effects.%s.%s", gExtraFxKeys[i], gExtraFxParams[i][p].field);
            const ExtraFxParamDef *def = &gExtraFxParams[i][p];
            float value = PlutoReadNumber(clipBegin, clipEnd, paramPath, def->initial);
            float span = fmaxf(.0001f, def->max - def->min);
            float normalized = (value - def->min) / span;
            gExtraFxV[i][p] = gExtraFxSent[i][p] = fmaxf(0.f, fminf(1.f, normalized));
        }
    }
    char preset[32] = {0};
    gFilter = 0;
    if (PlutoJsonStringPath(clipBegin, clipEnd, "props.effects.preset.id", preset, sizeof(preset)))
        for (int i = 0; i < 8; i++) if (strcmp(preset, presetNames[i]) == 0) gFilter = i;
    gFilterIntensity = gFilterIntensitySent = Clampf(PlutoReadNumber(clipBegin, clipEnd, "props.effects.preset.intensity", 100.f) / 100.f, 0.f, 1.f);
    strcpy(gTransitionType, "none");
    gTransitionDuration = PlutoReadNumber(clipBegin, clipEnd, "props.transitionOut.duration", 1.f);
    PlutoJsonStringPath(clipBegin, clipEnd, "props.transitionOut.type", gTransitionType, sizeof(gTransitionType));

    gTextTitleSelected = false;
    gTextIsSubtitle = false;
    gTextFontPath[0] = 0;
    snprintf(gTextFontName, sizeof(gTextFontName), "System");
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.isTitle", &gTextTitleSelected);
    PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.isSubtitle", &gTextIsSubtitle);
    gTextSubtitleSourceId[0] = 0;
    if (gTextTitleSelected) {
        if (gTextIsSubtitle)
            PlutoJsonStringPath(clipBegin, clipEnd, "props.text.sourceClipId", gTextSubtitleSourceId, sizeof(gTextSubtitleSourceId));
        PlutoJsonStringPath(clipBegin, clipEnd, "props.text.content", gTextContent, sizeof(gTextContent));
        PlutoJsonStringPath(clipBegin, clipEnd, "props.text.fontName", gTextFontName, sizeof(gTextFontName));
        PlutoJsonStringPath(clipBegin, clipEnd, "props.text.fontPath", gTextFontPath, sizeof(gTextFontPath));
        if (gTextFontPath[0] && !FileExists(gTextFontPath)) {
            gTextFontPath[0] = 0;
            snprintf(gTextFontName, sizeof(gTextFontName), "System");
        }
        gTextFontSize = (int)roundf(PlutoReadNumber(clipBegin, clipEnd, "props.text.fontSize", 96.f));
        gTextFontSize = (int)Clampf((float)gTextFontSize, 8.f, 180.f);
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
        gTextBoxW = Clampf(PlutoReadNumber(clipBegin, clipEnd, "props.text.boxW", .82f), .001f, 1.f);
        gTextBoxH = Clampf(PlutoReadNumber(clipBegin, clipEnd, "props.text.boxH", .32f), .001f, 1.f);
        gTextAutoFit = false;
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.autoFit", &gTextAutoFit);
        gTextDuration = Clampf(PlutoReadNumber(clipBegin, clipEnd, "dur", gTextDuration), gTextIsSubtitle ? .05f : .5f, 30.f);
        gTextBold = true; gTextItalic = false; gTextUnderline = false;
        gTextFontFamily = (int)roundf(PlutoReadNumber(clipBegin, clipEnd, "props.text.fontFamily", 0.f));
        if (gTextFontFamily < 0 || gTextFontFamily > 3) gTextFontFamily = 0;
        if (!gTextFontName[0] || (strcmp(gTextFontName, "System") == 0 && gTextFontFamily != 0)) {
            static const char *legacyFontNames[4] = {"System", "Arial", "Georgia", "Monospace"};
            snprintf(gTextFontName, sizeof(gTextFontName), "%s", legacyFontNames[gTextFontFamily]);
        }
        gTextFontMenuOpen = false;
        gTextFontMenuScroll = 0.f;
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.bold", &gTextBold);
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.italic", &gTextItalic);
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.underline", &gTextUnderline);
        gTextOutline = false; gTextShadow = true; gTextBackground = false;
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.outline", &gTextOutline);
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.shadow", &gTextShadow);
        PlutoJsonBoolPath(clipBegin, clipEnd, "props.text.background", &gTextBackground);

        /* Reuse the existing cropped title asset when only layout changes.
           Old full-canvas title cards intentionally regenerate on first edit. */
        double layoutScaleW = 0.0, layoutScaleH = 0.0;
        const bool hasCroppedLayout =
            PlutoJsonNumberPath(clipBegin, clipEnd, "props.text.layout.scale.w", &layoutScaleW) &&
            PlutoJsonNumberPath(clipBegin, clipEnd, "props.text.layout.scale.h", &layoutScaleH) &&
            layoutScaleW > 0.0 && layoutScaleH > 0.0 &&
            PlutoJsonStringRange(clipBegin, clipEnd, "path", gTextOutputPath, sizeof(gTextOutputPath)) &&
            FileExists(gTextOutputPath);
        gTextHasCroppedAsset = hasCroppedLayout;
        if (gTextHasCroppedAsset) gTextRenderedSignature = PlutoTextRasterSignature();
    }

    for (int i = 0; i < 4; i++) {
        float value = PlutoReadNumber(clipBegin, clipEnd, audioPaths[i], i == 0 ? 100.f : 0.f);
        float normalized = i == 0 ? StellaAudioKnobFromVolume(value) : value / (i == 1 ? 200.f : 24.f) + .5f;
        gKnob[i] = gKnobSent[i] = fmaxf(0.f, fminf(1.f, normalized));
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

/* Steppers fire once on press, then repeat while held. The caller receives
   begin/end edges so long presses can be committed as one undo operation. */
typedef struct { bool step, began, ended; } StellaArrowAction;
static uint32_t gArrowHeldId;
static double gArrowHeldAt, gArrowNextRepeatAt;
static StellaArrowAction Arrow(Clay_ElementId id, int icon) {
    StellaArrowAction action = {false, false, false};
    Bs b = Interact(id);
    bool over = Clay_PointerOver(id);
    double now = GetTime();
    if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        gArrowHeldId = id.id;
        gArrowHeldAt = now;
        gArrowNextRepeatAt = now + .28;
        action.step = action.began = true;
    } else if (gArrowHeldId == id.id) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || !over) {
            action.ended = true;
            gArrowHeldId = 0;
        } else if (now >= gArrowNextRepeatAt) {
            action.step = true;
            double heldFor = now - gArrowHeldAt;
            gArrowNextRepeatAt = now + (heldFor > 1.0 ? .030 : .055);
        }
    }
    CLAY(id, {.layout = {.sizing = {FIX(16), FIX(16)}, .childAlignment = AL_C}}) {
        Icon(icon, 16, Mix(C_ICON, C_TEXT, over ? fmaxf(.35f, b.hover) : 0.f));
    }
    return action;
}

static void CommitValueEdit(float *v) {
    if (gEditValueLen > 0) *v = strtof(gEditValueText, NULL);
    gEditValueId = 0;
}

static double gCaretT;   /* last edit activity: the caret stays solid while typing, then blinks */

static bool ValueFieldLogicEx(Clay_ElementId id, float *v, float dragSensitivity, bool allowNegative) {
    float original = *v;
    bool committed = false;
    bool valueChanged = false;
    bool over = Clay_PointerOver(id);
    if (dragSensitivity >= 0.f && (over || gValueDragId == id.id)) gScrubCursor = true;
    if (gEditValueId == id.id) {
        bool typed = false;
        if (IsKeyPressed(KEY_ENTER)) { CommitValueEdit(v); committed = true; valueChanged = fabsf(*v - original) > 0.001f; }
        else if (IsKeyPressed(KEY_BACKSPACE) && gEditValueLen > 0) { gEditValueText[--gEditValueLen] = 0; typed = true; }
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (((ch >= '0' && ch <= '9') || ch == '.' || (allowNegative && ch == '-' && gEditValueLen == 0)) &&
                gEditValueLen < (int)sizeof(gEditValueText) - 1) {
                gEditValueText[gEditValueLen++] = (char)ch;
                gEditValueText[gEditValueLen] = 0;
                typed = true;
            }
        }
        if (typed) {
            gCaretT = GetTime();
            /* Live update: the value follows every keystroke instead of waiting for Enter / click-away. */
            if (gEditValueLen > 0) {
                *v = strtof(gEditValueText, NULL);
                committed = true;
                valueChanged = fabsf(*v - original) > 0.001f;
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
                    gCaretT = GetTime();
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

/* Edit-mode text: the typed digits followed by a blinking caret bar. */
static void EditCaretText(float size, Clay_Color color) {
    if (gEditValueLen > 0) Tx(gEditValueText, size, color, 0);
    bool on = fmod(GetTime() - gCaretT, 1.0) < .55;
    CLAY_AUTO_ID({.layout = {.sizing = {FIX(2), FIX(roundf(size * 1.15f))}},
                  .backgroundColor = on ? color : (Clay_Color){0, 0, 0, 0},
                  .cornerRadius = CLAY_CORNER_RADIUS(1)}) {}
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
        if (gEditValueId == id.id) EditCaretText(fs + 2.f, C_TEXT);
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
static int gNumStepperField = -1;
static float gNumStepperStart;

static void NumFieldApplyLive(int n) {
    if (n < 0 || n >= 4) return;
    int group = n < 2 ? 0 : 1;
    if (gLink[group]) {
        int other = n ^ 1;
        if (n < 2) gNum[other] = gNum[n];
        else {
            float ratio = fabsf(gNumStepperStart) > .0001f ? gNum[n] / gNumStepperStart : 1.f;
            gNum[other] = fmaxf(0.f, gLinkedNumOtherStart * ratio);
        }
        char updates[384];
        snprintf(updates, sizeof(updates), "[{\"path\":\"%s\",\"value\":%.4f},{\"path\":\"%s\",\"value\":%.4f}]",
                 kNumPropertyPaths[n], gNum[n], kNumPropertyPaths[other], gNum[other]);
        PlutoTimelineSetPropertiesLiveJson(updates);
    } else {
        PlutoTimelineSetPropertyLive(kNumPropertyPaths[n], gNum[n]);
    }
    gPreviewDirty = true;
}

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
            if (gEditValueId == scrubId.id) EditCaretText(22, C_TEXT);
            else Tx(Fmt("%d", (int)roundf(*v)), 22, C_TEXT, 0);
            Tx(unit, 17, C_ICON, 0);
        }
        GAPX(3)
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}}}) {
            StellaArrowAction up = Arrow(CLAY_IDI("NumU", n), I_CHEV_UP);
            StellaArrowAction down = Arrow(CLAY_IDI("NumD", n), I_CHEV_DN);
            StellaArrowAction actions[2] = {up, down};
            for (int direction = 0; direction < 2; direction++) {
                StellaArrowAction a = actions[direction];
                int fieldKey = n * 2 + direction;
                if (a.began) {
                    gNumStepperField = fieldKey;
                    gNumStepperStart = *v;
                    gLinkedNumOtherStart = gNum[n ^ 1];
                    gLinkedNumDragField = n;
                }
                if (a.step && gNumStepperField == fieldKey) {
                    *v += direction == 0 ? 1.f : -1.f;
                    if (n < 2) *v = fmaxf(-100000.f, fminf(100000.f, *v));
                    else *v = fmaxf(0.f, fminf(100000.f, *v));
                    NumFieldApplyLive(n);
                }
                if (a.ended && gNumStepperField == fieldKey) {
                    CommitNumField(n, gNumStepperStart);
                    gNumStepperField = -1;
                    if (gLinkedNumDragField == n) gLinkedNumDragField = -1;
                }
            }
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
            if (gEditValueId == scrubId.id) EditCaretText(18, C_TEXT);
            else Tx(n < 4 ? Fmt("%.1f", gMini[n]) : Fmt("%.0f", gMini[n]), 18, C_TEXT, 0);
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
        /* Playback speed sits directly below Opacity; Crop/Distort remain lower. */
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 7}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(28)}, .childAlignment = AL_Y}}) {
                Tx("Speed", 18, C_TEXT, 0);
                HSPACE
                float speedValue = gSpeed;
                Clay_ElementId speedValueId = CLAY_ID("SpeedValue");
                if (ValBoxEditable(speedValueId, &speedValue, "x", 76, 27, 16, 13, 8, 0.25f, 10.0f, 2)) {
                    gSpeed = speedValue;
                    PlutoTimelineSetProperty("props.transform.speed", speedValue);
                    gPreviewDirty = true;
                }
                if (gValueDragging && gValueDragId == speedValueId.id) {
                    gSpeed = speedValue;
                    PlutoTimelineSetPropertyLive("props.transform.speed", speedValue);
                    gPreviewDirty = true;
                }
            }
        }
        CLAY_AUTO_ID({.layout = {GROW, CLAY_SIZING_GROW(10)}}) {}   /* min 10px, grows when the window is taller */
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
    static int previousHsl = -2;
    /* HSL uses the same three UI slider slots for nine independent hue bands.
       Re-seed the outbound cache whenever the active band changes, otherwise
       the previous band's cached progress can be mistaken for an unsent edit
       and written into the newly selected band. */
    if (!sentInit || previousSelected != gTimelineSelected || (idx == 1 && previousHsl != gHsl)) {
        memcpy(sent, gSlT, sizeof(sent));
        sentInit = true;
        previousSelected = gTimelineSelected;
        previousHsl = gHsl;
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
    if (b.clicked && gHsl != i) {
        /* A chip click may land on the same mouse-up frame as the previous
           slider drag. Commit that edit first: SetProperty otherwise inherits
           the live-edit flag and skips the full timeline sync/inspector hydrate,
           leaving the prior hue's slider progress active for the new hue. */
        PlutoTimelineEndPropertyEdit();
        gHsl = i;
        PlutoTimelineSetProperty("props.colors.hsl.selected", i);
    }
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
    {{239,151,112,255}, {49,67,117,255},  {255,226,152,255}},
    {{231,164,111,255}, {84,66,121,255},  {255,228,160,255}},
};
static const char *kFilterNames[8] = {"None", "Vivid", "Noir", "Fade", "Warm", "Cool", "Cinema", "Retro"};
static const char *kFilterIds[8] = {"none", "vivid", "noir", "fade", "warm", "cool", "cinema", "retro"};

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
static const FxDef kFx[VIDEO_FX_COUNT] = {
    {"Chromatic aberration", "Amount", "Angle", true},
    {"Pixelate", "Size", "Blend", false},
};
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

static double FxParamStoredValue(int i, int p, float value) {
    if (i == 0 && p == 1) return (value - .5f) * 360.0;
    if (i == 0 && p == 2) return (value - .5f) * 60.0;
    return value * 100.0;
}

static void FxParam(int i, int p, const char *label) {
    float *v = &gFxV[i][p];
    bool isAngle = (i == 0 && p == 1);
    bool isOffset = (i == 0 && p == 2);
    float minimum = (isAngle || isOffset) ? (isAngle ? -180.f : -30.f) : 0.f;
    float maximum = (isAngle || isOffset) ? (isAngle ? 180.f : 30.f) : 100.f;
    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(22)}, .childAlignment = AL_Y}}) {
            Tx(label, 13.5f, C_MUTED, 0);
            HSPACE
            float displayValue = minimum + *v * (maximum - minimum);
            if (ValBoxTyped(CLAY_IDI("FxValue", i * 3 + p), &displayValue,
                               isAngle ? "°" : isOffset ? "px" : NULL, 72, 22, 13.f, 11.f, 10,
                               minimum, maximum, 0)) {
                *v = fmaxf(0.f, fminf(1.f, (displayValue - minimum) / (maximum - minimum)));
                PlutoTimelineSetProperty(gFxProp[i][p], FxParamStoredValue(i, p, *v));
                gFxSent[i][p] = *v;
            }
        }
        Clay_ElementId sid = CLAY_IDI("FxSl", i * 3 + p);
        Bs bs = Interact(sid);
        FxSliderLogic(sid, v, &gFxSlDrag[i][p], 7.f);
        if (fabsf(*v - gFxSent[i][p]) > .0015f) {
            double value = FxParamStoredValue(i, p, *v);
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
                                 .backgroundColor = C_RCARD,
                                 .cornerRadius = {0.f, 0.f, 18.f, 18.f}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(1)}}, .backgroundColor = C_RCARDB}) {}
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(26)}, .childAlignment = AL_Y}}) {
            Clay_ElementId swid = CLAY_IDI("FxSw", i);
            Bs sb = Interact(swid);
            if (sb.clicked) {
                gFxOn[i] = !gFxOn[i];
                char updates[512]; size_t used = 0;
                used += (size_t)snprintf(updates + used, sizeof(updates) - used, "[{\"path\":\"%s\",\"value\":%s}",
                                          gFxToggle[i], gFxOn[i] ? "true" : "false");
                if (gFxOn[i]) for (int p = 0; p < gFxParamCount[i]; p++) {
                    used += (size_t)snprintf(updates + used, sizeof(updates) - used,
                                             ",{\"path\":\"%s\",\"value\":%.4f}",
                                             gFxProp[i][p], FxParamStoredValue(i, p, gFxV[i][p]));
                }
                if (used + 2 < sizeof(updates)) { updates[used++] = ']'; updates[used] = 0; PlutoTimelineSetPropertiesJson(updates); }
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
                gFxOn[i] = false;
                for (int p = 0; p < gFxParamCount[i]; p++) {
                    gFxV[i][p] = gFxSent[i][p] = (i == 0 && p == 1) ? .5f :
                                                     (i == 0 && p == 2) ? .5f : .3f;
                }
                if (i == 1) gFxV[i][1] = gFxSent[i][1] = .5f;
                char updates[512]; size_t used = 0;
                used += (size_t)snprintf(updates + used, sizeof(updates) - used, "[{\"path\":\"%s\",\"value\":false}", gFxToggle[i]);
                for (int p = 0; p < gFxParamCount[i]; p++)
                    used += (size_t)snprintf(updates + used, sizeof(updates) - used,
                                             ",{\"path\":\"%s\",\"value\":%.4f}",
                                             gFxProp[i][p], FxParamStoredValue(i, p, gFxV[i][p]));
                if (used + 2 < sizeof(updates)) { updates[used++] = ']'; updates[used] = 0; PlutoTimelineSetPropertiesJson(updates); }
            }
        }
        for (int p = 0; p < gFxParamCount[i]; p++) {
            static const char *labels[2][3] = {{"Amount", "Angle", "Offset"}, {"Size", "Blend", ""}};
            FxParam(i, p, labels[i][p]);
        }
    }
}

static bool EffectsSectionHeader(Clay_ElementId id, const char *title, int icon, bool open, bool modified, bool connected);

static void ExtraFxCommit(int i, bool includeParameters) {
    char updates[1024]; size_t used = 0;
    used += (size_t)snprintf(updates + used, sizeof(updates) - used,
                             "[{\"path\":\"props.effects.%s.enabled\",\"value\":%s}",
                             gExtraFxKeys[i], gExtraFxOn[i] ? "true" : "false");
    if (includeParameters) {
        for (int p = 0; p < gExtraFxParamCount[i]; p++) {
            const ExtraFxParamDef *def = &gExtraFxParams[i][p];
            double value = def->min + gExtraFxV[i][p] * (def->max - def->min);
            used += (size_t)snprintf(updates + used, sizeof(updates) - used,
                                     ",{\"path\":\"props.effects.%s.%s\",\"value\":%.4f}",
                                     gExtraFxKeys[i], def->field, value);
        }
        if (i == 0) {
            const char *mode = gBlurMode == 1 ? "motion" : gBlurMode == 2 ? "zoom" : "gaussian";
            used += (size_t)snprintf(updates + used, sizeof(updates) - used,
                                     ",{\"path\":\"props.effects.blur.mode\",\"value\":\"%s\"}", mode);
        }
    }
    if (used + 2 < sizeof(updates)) { updates[used++] = ']'; updates[used] = 0; PlutoTimelineSetPropertiesJson(updates); }
}

static void ExtraFxSetDefaults(int i) {
    for (int p = 0; p < gExtraFxParamCount[i]; p++) {
        const ExtraFxParamDef *def = &gExtraFxParams[i][p];
        float normalized = (def->initial - def->min) / fmaxf(.0001f, def->max - def->min);
        gExtraFxV[i][p] = gExtraFxSent[i][p] = fmaxf(0.f, fminf(1.f, normalized));
    }
}

static void ExtraFxParam(int i, int p) {
    const ExtraFxParamDef *def = &gExtraFxParams[i][p];
    float *value = &gExtraFxV[i][p];
    float actual = def->min + *value * (def->max - def->min);
    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(22)}, .childAlignment = AL_Y}}) {
            Tx(def->label, 13.5f, C_MUTED, 0);
            HSPACE
            float displayValue = actual;
            if (ValBoxTyped(CLAY_IDI("FxExtraValue", i * 4 + p), &displayValue,
                            def->suffix, 72, 22, 13.f, 11.f, 10, def->min, def->max, 0)) {
                *value = fmaxf(0.f, fminf(1.f, (displayValue - def->min) / (def->max - def->min)));
                char path[160]; snprintf(path, sizeof(path), "props.effects.%s.%s", gExtraFxKeys[i], def->field);
                PlutoTimelineSetProperty(path, def->min + *value * (def->max - def->min));
                gExtraFxSent[i][p] = *value;
            }
        }
        Clay_ElementId sid = CLAY_IDI("FxExtraSlider", i * 4 + p);
        Bs sb = Interact(sid);
        FxSliderLogic(sid, value, &gExtraFxSlDrag[i][p], 7.f);
        if (fabsf(*value - gExtraFxSent[i][p]) > .0015f) {
            char path[160]; snprintf(path, sizeof(path), "props.effects.%s.%s", gExtraFxKeys[i], def->field);
            double actualValue = def->min + *value * (def->max - def->min);
            if (gExtraFxSlDrag[i][p]) PlutoTimelineSetPropertyLive(path, actualValue);
            else PlutoTimelineSetProperty(path, actualValue);
            gExtraFxSent[i][p] = *value;
        }
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(16)}},
                   .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD, *value,
                                               (sb.hover + (gExtraFxSlDrag[i][p] ? 1.f : 0.f)) * 1.5f, 0)}}) {}
    }
}

static void ExtraFxBody(int i) {
    CLAY(CLAY_IDI("FxExtraBody", i), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {14, 12, 12, 12}, .childGap = 10},
                                      .backgroundColor = C_RCARD,
                                      .cornerRadius = {0.f, 0.f, 18.f, 18.f}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(1)}}, .backgroundColor = C_RCARDB}) {}
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(26)}, .childAlignment = AL_Y}}) {
            Clay_ElementId swid = CLAY_IDI("FxExtraEnabled", i);
            Bs sb = Interact(swid);
            if (sb.clicked) {
                gExtraFxOn[i] = !gExtraFxOn[i];
                ExtraFxCommit(i, gExtraFxOn[i]);
            }
            float enabledAnim = Anim(swid.id + 5u, gExtraFxOn[i] ? 1.f : 0.f, 22.f);
            CLAY(swid, {.layout = {.sizing = {FIT, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(38), FIX(22)}},
                              .custom = {.customData = NC(K_SWITCH, C_CARD, C_CARD, C_CARD, enabledAnim,
                                                          sb.hover - sb.press * 1.5f, 0)}}) {}
                Tx("Enabled", 13.5f, Mix(C_MUTED, C_DIM, sb.hover), 0);
            }
            HSPACE
        }
        if (i == 0) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(34)}, .childGap = 5}}) {
                const char *modes[3] = {"Gaussian", "Motion", "Zoom"};
                for (int mode = 0; mode < 3; mode++) {
                    Clay_ElementId modeId = CLAY_IDI("BlurMode", mode);
                    Bs modeButton = Interact(modeId);
                    if (modeButton.clicked) {
                        gBlurMode = mode;
                        const char *stored = mode == 1 ? "motion" : mode == 2 ? "zoom" : "gaussian";
                        char updates[128];
                        snprintf(updates, sizeof(updates), "[{\"path\":\"props.effects.blur.mode\",\"value\":\"%s\"}]", stored);
                        PlutoTimelineSetPropertiesJson(updates);
                    }
                    CLAY(modeId, {.layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C},
                                  .backgroundColor = gBlurMode == mode ? RGB(37, 38, 43) : RGB(19, 20, 23),
                                  .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
                        Tx(modes[mode], 12.5f, gBlurMode == mode ? C_TEXT : C_MUTED, 0);
                    }
                }
            }
        }
        if (gExtraFxParamCount[i] == 0) {
            Tx("Flips the clip horizontally.", 13.5f, C_MUTED, 0);
        } else {
            for (int p = 0; p < gExtraFxParamCount[i]; p++) ExtraFxParam(i, p);
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(24)}, .childAlignment = AL_Y}}) {
            HSPACE
            Clay_ElementId rid = CLAY_IDI("FxExtraReset", i);
            Bs rb = Interact(rid);
            Clay_Color rc = Mix(RGB(140, 142, 150), C_TEXT, rb.hover);
            CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                ResetIcon(rid, I_RESET, 14, rc, rb.clicked);
                Tx("Reset", 13.5f, rc, 0);
            }
            if (rb.clicked) {
                ExtraFxSetDefaults(i);
                gExtraFxOn[i] = false;
                ExtraFxCommit(i, true);
            }
        }
    }
}

static bool EffectsSectionHeader(Clay_ElementId id, const char *title, int icon, bool open, bool modified, bool connected);

static void ExtraFxRow(int i) {
    Clay_ElementId header = CLAY_IDI("FxExtraHdr", i);
    bool targetOpen = gExtraFxOpen == i;
    float openAnim = EaseIO(AnimLin(header.id + 0x319u, targetOpen ? 1.f : 0.f, 0.2f));
    float measured = Clay_GetElementData(CLAY_IDI("FxExtraBody", i)).boundingBox.height;
    if (measured > 1.f) gExtraFxBodyHeight[i] = measured;
    float wrapHeight = roundf(openAnim * gExtraFxBodyHeight[i]);
    CLAY(CLAY_IDI("FxExtraItem", i), {.layout = {COLM, .sizing = {GROW, FIT}},
                                      .backgroundColor = C_RCARD,
                                      .cornerRadius = CLAY_CORNER_RADIUS(16),
                                      .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        if (EffectsSectionHeader(header, gExtraFxNames[i], I_SPARKLE,
                                 targetOpen, gExtraFxOn[i], true))
            gExtraFxOpen = targetOpen ? -1 : i;
        if (wrapHeight >= 1.f) {
            CLAY(CLAY_IDI("FxExtraBodyWrap", i), {.layout = {COLM, .sizing = {GROW, FIX(wrapHeight)}},
                                                   .clip = {.vertical = true}}) {
                ExtraFxBody(i);
            }
        }
    }
}

/* Effect rows use the same 48px accordion card as the Colors panel. */
static bool FxHeader(int i, float open) {
    Clay_ElementId id = CLAY_IDI("FxHdr", i);
    return EffectsSectionHeader(id, kFx[i].name, I_SPARKLE, open > 0.5f, gFxOn[i], true);
}

static float gEffectSectionHeight[3] = {96.f, 286.f, 1220.f};
static float gFxBodyHeight[VIDEO_FX_COUNT] = {230.f, 180.f};
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
                  .cornerRadius = connected ? (Clay_CornerRadius){18.f, 18.f, 18.f * (1.f - openAnim), 18.f * (1.f - openAnim)}
                                            : CLAY_CORNER_RADIUS(18),
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
    if (from < 0 || from >= gTimelineClipCount || gTimelineClips[from].track >= TIMELINE_MAX_VIDEO_TRACKS ||
        gTimelineClips[from].isSubtitle) return -1;
    float end = gTimelineClips[from].start + gTimelineClips[from].duration;
    int next = -1;
    float nextStart = INFINITY;
    /* Match the backend's same-track chronological neighbor pairing. Looking
       for the smallest absolute distance can accidentally pick an overlapping
       previous clip and write a transition to the wrong side of the edit. */
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (i == from || gTimelineClips[i].isSubtitle ||
            gTimelineClips[i].track != gTimelineClips[from].track) continue;
        float candidateStart = gTimelineClips[i].start;
        if (candidateStart <= gTimelineClips[from].start + .001f || candidateStart >= nextStart) continue;
        next = i;
        nextStart = candidateStart;
    }
    return next >= 0 && fabsf(nextStart - end) <= .04f ? next : -1;
}

static void CommitTransition(void) {
    /* The backend applies properties to gTimelineSelected. The transition badge
       already selects its outgoing clip, so never write to a stale focus index. */
    int from = gTimelineSelected;
    int next = TransitionNextClip(from);
    if (from < 0 || next < 0) return;
    gTransitionDuration = StellaTransitionEffectiveDuration(
        gTransitionDuration, gTimelineClips[from].duration, gTimelineClips[next].duration);
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
    int from = gTimelineSelected;
    int next = TransitionNextClip(from);

    /* Let the Effects page share the exact content width supplied by InspContent.
       A second, manually calculated width used to add the inspector padding twice
       and made the expanded empty-transition state push the whole column sideways. */
    static int measuredFrom = -2, measuredNext = -2;
    bool transitionContextChanged = from != measuredFrom || next != measuredNext;
    if (transitionContextChanged) {
        measuredFrom = from;
        measuredNext = next;
        gTransitionBodyMeasured = false;
        gEffectSectionHeight[0] = next < 0 ? 96.f : 355.f;
    }
    CLAY(CLAY_ID("EffectsRoot"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10}}) {
        /* Transitions: a measured-height wrapper animates the content just
           like the Colors accordions, with the same 1px border and radius. */
        CLAY(CLAY_ID("TransCol"), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        Clay_ElementId transId = CLAY_ID("TransHdr");
        float transOpen = EaseIO(AnimLin(transId.id, gTransOpen ? 1.f : 0.f, 0.2f));
        float transMeasured = Clay_GetElementData(CLAY_ID("TransBody")).boundingBox.height;
        if (!transitionContextChanged && transMeasured > 1.f) {
            gEffectSectionHeight[0] = transMeasured;
            gTransitionBodyMeasured = true;
        } else if (!gTransitionBodyMeasured) {
            gEffectSectionHeight[0] = next < 0 ? 96.f : 355.f;
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
                                                  .padding = {14, 14, 12, 12}, .childGap = 8},
                                                  .clip = {.horizontal = true}, RCARD}) {
                    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(1)}}, .backgroundColor = C_RCARDB}) {}
                    if (next < 0) {
                        CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 2}}) {
                            /* Keep the hint deliberately short and split into lines so its
                               intrinsic text width cannot expand the entire Effects column. */
                            Tx("Add a second video clip.", 14, C_MUTED, 0);
                            Tx("Place it after this clip.", 14, C_MUTED, 0);
                            Tx("Use the plus at their join.", 14, C_MUTED, 0);
                        }
                    } else {
                        CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 1}}) {
                            TxWrap(Fmt("%.26s", gTimelineClips[from].label), 13, C_MUTED, 0);
                            TxWrap(Fmt("to %.26s", gTimelineClips[next].label), 13, C_MUTED, 0);
                        }
                        const char *names[10] = {"None", "Cross dissolve", "Dip to black", "Wipe", "Iris", "Slide", "Zoom", "Spin", "Blur", "Checkerboard"};
                        const char *keys[10] = {"none", "crossfade", "dip", "wipe", "iris", "slide", "zoom", "spin", "blur", "checker"};
                        for (int row = 0; row < 5; row++) {
                            CLAY(CLAY_IDI("TransitionChoiceRow", row),
                                 {.layout = {.sizing = {GROW, FIX(42)}, .childGap = 5}}) {
                                for (int col = 0; col < 2; col++) {
                                    int i = row * 2 + col;
                                    if (i >= 10) continue;
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
                        float transitionMax = StellaTransitionMaxDuration(
                            gTimelineClips[from].duration, gTimelineClips[next].duration);
                        float transitionMin = fminf(.05f, transitionMax);
                        gTransitionDuration = Clampf(gTransitionDuration, transitionMin, transitionMax);
                        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(22)}, .childAlignment = AL_Y}}) {
                            Tx("Duration", 13, C_MUTED, 0); HSPACE;
                            float durationValue = gTransitionDuration;
                            if (ValBoxEditable(CLAY_ID("TransitionDurationValue"), &durationValue,
                                               "s", 72, 22, 13, 11, 10, transitionMin, transitionMax, 2)) {
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
                                gTransitionDuration = transitionMin + (transitionMax - transitionMin) *
                                    Clampf((GetMousePosition().x - bb.x) / bb.width, 0.f, 1.f);
                        }
                        if (durationDrag && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                            durationDrag = false;
                            CommitTransition();
                        }
                        CLAY(did, {.layout = {.sizing = {GROW, FIX(16)}},
                                   .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160),
                                                               C_CARD, (gTransitionDuration - transitionMin) /
                                                               fmaxf(.0001f, transitionMax - transitionMin),
                                                               durationDrag ? 1.f : Clay_PointerOver(did), 0)}}) {}
                    }
                }
            }
        }
        }

        /* Filters: eight practical presets, no category pills or hidden rows. */
        CLAY(CLAY_ID("FiltersCol"), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        Clay_ElementId filtersId = CLAY_ID("FiltersHdr");
        float filtersOpen = EaseIO(AnimLin(filtersId.id, gFiltOpen ? 1.f : 0.f, 0.2f));
        float filtersMeasured = Clay_GetElementData(CLAY_ID("FiltBody")).boundingBox.height;
        if (filtersMeasured > 1.f) gEffectSectionHeight[1] = filtersMeasured;
        float filtersWrapHeight = roundf(filtersOpen * (gEffectSectionHeight[1] + 10.f));
        if (EffectsSectionHeader(filtersId, "Filters", I_SPARKLE, gFiltOpen,
                                 gFilter != 0, false)) gFiltOpen = !gFiltOpen;
        if (filtersWrapHeight >= 1.f) {
            CLAY(CLAY_ID("FiltersWrap"), {.layout = {COLM, .sizing = {GROW, FIX(filtersWrapHeight)}},
                                          .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_ID("FiltBody"), {.layout = {COLM, .sizing = {GROW, FIT},
                                                 .padding = {12, 12, 10, 10}, .childGap = 10}, RCARD}) {
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
                        CLAY(CLAY_IDI("FltRow", row), {.layout = {.sizing = {GROW, FIX(78)}, .childGap = 7}}) {
                            for (int col = 0; col < 4; col++) FilterThumb(row * 4 + col);
                        }
                    }
                    FilterIntensityControl();
                    ExtraFxRow(0);
                    ExtraFxRow(1);
                }
            }
        }
        }

        /* Video Effects: section expansion and each individual effect use
           measured-height animation. Every effect's header and controls share
           one bordered card so the expanded rows stay visually connected. */
        bool anyVideoEffect = false;
        for (int i = 0; i < VIDEO_FX_COUNT; i++) anyVideoEffect = anyVideoEffect || gFxOn[i];
        for (int i = 2; i < EXTRA_FX_COUNT; i++) anyVideoEffect = anyVideoEffect || gExtraFxOn[i];
        CLAY(CLAY_ID("VfxCol"), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        Clay_ElementId vfxId = CLAY_ID("VfxHdr");
        float vfxOpen = EaseIO(AnimLin(vfxId.id, gVfxOpen ? 1.f : 0.f, 0.2f));
        float vfxMeasured = Clay_GetElementData(CLAY_ID("VfxBody")).boundingBox.height;
        if (vfxMeasured > 1.f) gEffectSectionHeight[2] = vfxMeasured;
        float vfxWrapHeight = roundf(vfxOpen * (gEffectSectionHeight[2] + 10.f));
        if (EffectsSectionHeader(vfxId, "Video Effects", I_FILM, gVfxOpen,
                                 anyVideoEffect, false)) gVfxOpen = !gVfxOpen;
        if (vfxWrapHeight >= 1.f) {
            CLAY(CLAY_ID("VfxWrap"), {.layout = {COLM, .sizing = {GROW, FIX(vfxWrapHeight)}},
                                      .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_ID("VfxBody"), {.layout = {COLM, .sizing = {GROW, FIT},
                                               .padding = {10, 10, 10, 10}, .childGap = 8}, RCARD}) {
                    for (int i = 0; i < VIDEO_FX_COUNT; i++) {
                        Clay_ElementId fxId = CLAY_IDI("FxHdr", i);
                        float fxOpen = EaseIO(AnimLin(fxId.id, gFxOpen == i ? 1.f : 0.f, 0.2f));
                        float fxMeasured = Clay_GetElementData(CLAY_IDI("FxBody", i)).boundingBox.height;
                        if (fxMeasured > 1.f) gFxBodyHeight[i] = fxMeasured;
                        float fxWrapHeight = roundf(fxOpen * gFxBodyHeight[i]);
                        CLAY(CLAY_IDI("FxItem", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 0},
                                                       .backgroundColor = C_RCARD,
                                                       .cornerRadius = CLAY_CORNER_RADIUS(18),
                                                       .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)}}) {
                            if (FxHeader(i, gFxOpen == i ? 1.f : 0.f))
                                gFxOpen = (gFxOpen == i) ? -1 : i;
                            if (fxWrapHeight >= 1.f) {
                                CLAY(CLAY_IDI("FxBodyWrap", i),
                                     {.layout = {COLM, .sizing = {GROW, FIX(fxWrapHeight)}}}) {
                                    FxBody(i);
                                }
                            }
                        }
                    }
                    for (int i = 2; i < EXTRA_FX_COUNT; i++) ExtraFxRow(i);
                }
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
    gTextCaretT = GetTime();
}

static void PlutoTextBackspace(void) {
    size_t n = strlen(gTextContent);
    if (!n) return;
    n--;
    while (n > 0 && (((unsigned char)gTextContent[n] & 0xc0u) == 0x80u)) n--;
    gTextContent[n] = 0;
    gTextCaretT = GetTime();
}

#define STELLA_TEXT_MAX_LINES 64

static int PlutoTextBuildLines(Font font, int fontSize, float maxWidth,
                               char lines[STELLA_TEXT_MAX_LINES][STELLA_TEXT_MAX]) {
    char source[STELLA_TEXT_MAX];
    strncpy(source, gTextContent, sizeof(source) - 1);
    source[sizeof(source) - 1] = 0;
    int count = 0;
    char *lineStart = source;
    while (lineStart && count < STELLA_TEXT_MAX_LINES) {
        char *newline = strchr(lineStart, '\n');
        if (newline) *newline = 0;
        char current[STELLA_TEXT_MAX] = {0};
        char *word = lineStart;
        while (*word && count < STELLA_TEXT_MAX_LINES) {
            while (*word == ' ' || *word == '\t' || *word == '\r') word++;
            if (!*word) break;
            char *wordEnd = word;
            while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\t' && *wordEnd != '\r') wordEnd++;
            char saved = *wordEnd;
            *wordEnd = 0;

            char candidate[STELLA_TEXT_MAX];
            snprintf(candidate, sizeof(candidate), "%s%s%s", current,
                     current[0] ? " " : "", word);
            float candidateWidth = MeasureTextEx(font, candidate, (float)fontSize, 0.f).x;
            if (current[0] && candidateWidth > maxWidth) {
                strncpy(lines[count++], current, STELLA_TEXT_MAX - 1);
                lines[count - 1][STELLA_TEXT_MAX - 1] = 0;
                current[0] = 0;
            }

            /* A single long word must wrap too; otherwise it can escape a
               narrow text frame even after the font has reached its minimum. */
            if (MeasureTextEx(font, word, (float)fontSize, 0.f).x > maxWidth) {
                char fragment[STELLA_TEXT_MAX] = {0};
                const char *cursor = word;
                while (*cursor && count < STELLA_TEXT_MAX_LINES) {
                    int codepointBytes = 0;
                    GetCodepointNext(cursor, &codepointBytes);
                    if (codepointBytes < 1 || codepointBytes > 4) codepointBytes = 1;
                    char piece[5] = {0};
                    memcpy(piece, cursor, (size_t)codepointBytes);
                    char fragmentCandidate[STELLA_TEXT_MAX];
                    snprintf(fragmentCandidate, sizeof(fragmentCandidate), "%s%s", fragment, piece);
                    if (fragment[0] && MeasureTextEx(font, fragmentCandidate, (float)fontSize, 0.f).x > maxWidth) {
                        strncpy(lines[count++], fragment, STELLA_TEXT_MAX - 1);
                        lines[count - 1][STELLA_TEXT_MAX - 1] = 0;
                        fragment[0] = 0;
                    }
                    strncat(fragment, piece, sizeof(fragment) - strlen(fragment) - 1);
                    cursor += codepointBytes;
                }
                strncpy(current, fragment, sizeof(current) - 1);
                current[sizeof(current) - 1] = 0;
            } else if (current[0]) {
                strncpy(current, candidate, sizeof(current) - 1);
                current[sizeof(current) - 1] = 0;
            } else {
                strncpy(current, word, sizeof(current) - 1);
                current[sizeof(current) - 1] = 0;
            }

            *wordEnd = saved;
            word = wordEnd;
        }
        if (count < STELLA_TEXT_MAX_LINES) {
            strncpy(lines[count], current, STELLA_TEXT_MAX - 1);
            lines[count][STELLA_TEXT_MAX - 1] = 0;
            count++;
        }
        lineStart = newline ? newline + 1 : NULL;
    }
    return count > 0 ? count : 1;
}

static int PlutoTextFontSlot(void) {
    int family = gTextFontFamily < 0 || gTextFontFamily > 3 ? 0 : gTextFontFamily;
    return (gTextBold ? 7 : 3) + family;
}

static Font PlutoTextFontAt(int px) {
    StellaFontEnsureTextCodepoints(gTextContent);
    if (gTextFontPath[0] && FileExists(gTextFontPath)) return FontPxFromPath(gTextFontPath, px);
    return FontPx(PlutoTextFontSlot(), px);
}

#ifdef _WIN32
static bool PlutoFontPathIsAbsolute(const wchar_t *path) {
    return path && path[0] && ((path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
                              (path[0] == L'\\' && path[1] == L'\\'));
}

static void PlutoFontAddSystemEntry(const wchar_t *rawName, const wchar_t *rawPath) {
    if (!rawName || !rawPath || !*rawPath || gSystemFontCount >= STELLA_MAX_SYSTEM_FONTS) return;
    wchar_t name[256], path[1024], expanded[1024], windowsDir[MAX_PATH], candidate[1024];
    wcsncpy(name, rawName, 255); name[255] = 0;
    wchar_t *suffix = wcsstr(name, L" (TrueType)"); if (suffix) *suffix = 0;
    suffix = wcsstr(name, L" (OpenType)"); if (suffix) *suffix = 0;
    if (!name[0]) return;
    if (wcslen(rawPath) >= 1024) return;
    wcscpy(path, rawPath);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        DWORD needed = ExpandEnvironmentStringsW(rawPath, expanded, 1024);
        if (needed > 0 && needed < 1024) wcscpy(path, expanded);
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && !PlutoFontPathIsAbsolute(path)) {
        UINT n = GetWindowsDirectoryW(windowsDir, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            swprintf(candidate, 1024, L"%ls\\Fonts\\%ls", windowsDir, path);
            if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) wcscpy(path, candidate);
        }
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && !PlutoFontPathIsAbsolute(path)) {
        wchar_t localAppData[1024]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, 1024);
        if (n > 0 && n < 1024) {
            swprintf(candidate, 1024, L"%ls\\Microsoft\\Windows\\Fonts\\%ls", localAppData, path);
            if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) wcscpy(path, candidate);
        }
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
    const wchar_t *extension = wcsrchr(path, L'.');
    if (!extension || (_wcsicmp(extension, L".ttf") != 0 && _wcsicmp(extension, L".otf") != 0)) return;
    char utfName[96], utfPath[1024];
    int nameBytes = WideCharToMultiByte(CP_UTF8, 0, name, -1, utfName, sizeof(utfName), NULL, NULL);
    int pathBytes = WideCharToMultiByte(CP_UTF8, 0, path, -1, utfPath, sizeof(utfPath), NULL, NULL);
    if (nameBytes <= 1 || pathBytes <= 1) return;
    for (int i = 0; i < gSystemFontCount; i++)
        if (_stricmp(gSystemFonts[i].path, utfPath) == 0) return;
    StellaSystemFont *entry = &gSystemFonts[gSystemFontCount++];
    snprintf(entry->name, sizeof(entry->name), "%s", utfName);
    snprintf(entry->path, sizeof(entry->path), "%s", utfPath);
}

static void PlutoFontReadRegistryKey(HKEY root, const wchar_t *subKey) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    for (DWORD index = 0; index < 4096 && gSystemFontCount < STELLA_MAX_SYSTEM_FONTS; index++) {
        wchar_t valueName[256] = {0}; wchar_t valueData[1024] = {0};
        DWORD nameChars = (DWORD)(sizeof(valueName) / sizeof(valueName[0]));
        DWORD dataBytes = (DWORD)sizeof(valueData), type = 0;
        LONG result = RegEnumValueW(key, index, valueName, &nameChars, NULL, &type,
                                    (LPBYTE)valueData, &dataBytes);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)) continue;
        if (valueData[0]) PlutoFontAddSystemEntry(valueName, valueData);
    }
    RegCloseKey(key);
}

static int PlutoCompareSystemFonts(const void *a, const void *b) {
    const StellaSystemFont *fa = (const StellaSystemFont *)a;
    const StellaSystemFont *fb = (const StellaSystemFont *)b;
    return _stricmp(fa->name, fb->name);
}
#endif

static void PlutoEnumerateSystemFonts(void) {
    gSystemFontCount = 0;
#ifdef _WIN32
    PlutoFontReadRegistryKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts");
    PlutoFontReadRegistryKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts");
    PlutoFontReadRegistryKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Fonts");
    if (gSystemFontCount > 1)
        qsort(gSystemFonts, (size_t)gSystemFontCount, sizeof(gSystemFonts[0]), PlutoCompareSystemFonts);
#endif
}

/* Keep title-frame caret geometry in the same 1920x1080 reference space as
   the rasterized title and its WGPU transform. This is an editor-only overlay. */
static void PlutoDrawTextCaret(void) {
    if (!gTextInputFocused || !gTextTitleSelected || !gTextBoxScreenRectValid) return;
    double elapsed = GetTime() - gTextCaretT;
    if (elapsed > 0.58 && fmod(elapsed, 1.08) > 0.58) return;
    const float boxW = Clampf(gTextBoxW, .001f, 1.f) * 1920.f;
    const float boxH = Clampf(gTextBoxH, .001f, 1.f) * 1080.f;
    const float padX = fmaxf(5.f, boxW * .055f);
    const float padY = fmaxf(4.f, boxH * .09f);
    const float availableW = fmaxf(1.f, boxW - 2.f * padX);
    const float availableH = fmaxf(1.f, boxH - 2.f * padY);
    Font font = PlutoTextFontAt(180);
    int fontSize = (int)Clampf((float)gTextFontSize, 8.f, 180.f);
    char lines[STELLA_TEXT_MAX_LINES][STELLA_TEXT_MAX] = {{0}};
    int count = PlutoTextBuildLines(font, fontSize, availableW, lines);
    float lineHeight = (float)fontSize * 1.18f;
    float blockHeight = (float)count * lineHeight;
    float firstY = fmaxf(padY, (boxH - blockHeight) * .5f);
    const char *lastLine = lines[count - 1];
    float textWidth = MeasureTextEx(font, lastLine, (float)fontSize, 0.f).x;
    float drawX = gTextAlign == 0 ? padX :
                  gTextAlign == 1 ? (boxW - textWidth) * .5f : boxW - padX - textWidth;
    drawX = Clampf(drawX, 0.f, fmaxf(0.f, boxW - textWidth));
    float scale = gTextBoxScreenRect.width / fmaxf(1.f, boxW);
    float x = gTextBoxScreenRect.x + (drawX + textWidth) * scale;
    float y = gTextBoxScreenRect.y + (firstY + (float)(count - 1) * lineHeight) * scale;
    float h = fmaxf(8.f, (float)fontSize * .96f * scale);
    DrawLineEx((Vector2){x, y}, (Vector2){x, y + h}, fmaxf(1.4f, 1.6f * scale),
               (Color){248, 249, 252, 245});
}

static unsigned long long PlutoTextPathHash(const char *value) {
    unsigned long long hash = 1469598103934665603ull;
    if (!value) return hash;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        hash ^= *p;
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool PlutoTextFallbackOutputPath(char *path, size_t capacity);

static bool PlutoTextOutputPath(char *path, size_t capacity, bool forceNew) {
    const char *clipId = (!forceNew && gTextTitleSelected &&
                          gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount)
        ? gTimelineClips[gTimelineSelected].id : NULL;
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
    DWORD folderAttributes = GetFileAttributesA(folder);
    if (folderAttributes == INVALID_FILE_ATTRIBUTES || !(folderAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        DWORD tempLength = GetTempPathA((DWORD)sizeof(root), root);
        if (!tempLength || tempLength >= sizeof(root)) return false;
        size_t rootLength = strlen(root);
        while (rootLength > 0 && (root[rootLength - 1] == '\\' || root[rootLength - 1] == '/'))
            root[--rootLength] = 0;
        if (snprintf(parent, sizeof(parent), "%s\\Stella", root) >= (int)sizeof(parent) ||
            snprintf(folder, sizeof(folder), "%s\\Titles", parent) >= (int)sizeof(folder)) return false;
        CreateDirectoryA(parent, NULL);
        CreateDirectoryA(folder, NULL);
        folderAttributes = GetFileAttributesA(folder);
        if (folderAttributes == INVALID_FILE_ATTRIBUTES || !(folderAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    }
    if (clipId && *clipId) {
        unsigned long long hash = PlutoTextPathHash(clipId);
        /* The preview decoder and texture cache are path-keyed. Every revision
           gets a fresh path, so an older cached PNG can never mask new text. */
        for (int attempt = 0; attempt < 512; attempt++) {
            unsigned long long serial = (unsigned long long)gClipSerial++;
            int n = snprintf(path, capacity, "%s\\title-%016llx-%llu.png", folder, hash, serial);
            if (n <= 0 || (size_t)n >= capacity) return false;
            if (!FileExists(path) && !PlutoPreviewPathInFlight(path)) return true;
        }
        return PlutoTextFallbackOutputPath(path, capacity);
    }
    for (int attempt = 0; attempt < 32; attempt++) {
        char temporary[MAX_PATH];
        if (!GetTempFileNameA(folder, "stl", 0, temporary)) return false;
        DeleteFileA(temporary);
        char *extension = strrchr(temporary, '.');
        if (!extension) return false;
        strcpy(extension, ".png");
        if (FileExists(temporary) || PlutoPreviewPathInFlight(temporary)) continue;
        int n = snprintf(path, capacity, "%s", temporary);
        return n > 0 && (size_t)n < capacity;
    }
    return PlutoTextFallbackOutputPath(path, capacity);
#else
    unsigned long long hash = clipId && *clipId ? PlutoTextPathHash(clipId) : 0ull;
    for (int attempt = 0; attempt < 512; attempt++) {
        unsigned long long serial = (unsigned long long)gClipSerial++;
        int n = clipId && *clipId
            ? snprintf(path, capacity, "Stella-title-%016llx-%llu.png", hash, serial)
            : snprintf(path, capacity, "Stella-title-%llu.png", serial);
        if (n <= 0 || (size_t)n >= capacity) return false;
        if (!FileExists(path) && !PlutoPreviewPathInFlight(path)) return true;
    }
    return PlutoTextFallbackOutputPath(path, capacity);
#endif
}

static void PlutoTextShearGlyphs(Image *image, float maxShear, float lineHeight, float firstLineY) {
    if (!image || !image->data || image->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 || maxShear <= 0.f) return;
    const int width = image->width, height = image->height;
    const size_t pixelCount = (size_t)width * (size_t)height;
    Color *source = (Color *)image->data;
    Color *shifted = (Color *)calloc(pixelCount, sizeof(Color));
    if (!shifted) return;
    const float safeLineHeight = fmaxf(1.f, lineHeight);
    for (int y = 0; y < height; y++) {
        float withinLine = fmodf(fmaxf(0.f, (float)y - firstLineY), safeLineHeight) / safeLineHeight;
        int offset = (int)roundf(maxShear * (1.f - withinLine));
        for (int x = 0; x < width; x++) {
            Color pixel = source[(size_t)y * width + x];
            if (pixel.a == 0) continue;
            int destinationX = x + offset;
            if (destinationX >= 0 && destinationX < width)
                shifted[(size_t)y * width + destinationX] = pixel;
        }
    }
    memcpy(source, shifted, pixelCount * sizeof(Color));
    free(shifted);
}

static uint64_t PlutoTextFitSignature(void) {
    uint64_t hash = 1469598103934665603ull;
#define TEXT_FIT_HASH_BYTES(value) do { \
    const unsigned char *bytes = (const unsigned char *)&(value); \
    for (size_t byteIndex = 0; byteIndex < sizeof(value); byteIndex++) { hash ^= bytes[byteIndex]; hash *= 1099511628211ull; } \
} while (0)
    const unsigned char *textBytes = (const unsigned char *)gTextContent;
    for (size_t byteIndex = 0; byteIndex <= strlen(gTextContent); byteIndex++) {
        hash ^= textBytes[byteIndex]; hash *= 1099511628211ull;
    }
    TEXT_FIT_HASH_BYTES(gTextFontFamily);
    for (size_t fontByte = 0; fontByte < sizeof(gTextFontPath); fontByte++) { hash ^= (unsigned char)gTextFontPath[fontByte]; hash *= 1099511628211ull; }
    TEXT_FIT_HASH_BYTES(gTextBold);
    TEXT_FIT_HASH_BYTES(gTextBoxW);
    TEXT_FIT_HASH_BYTES(gTextBoxH);
    TEXT_FIT_HASH_BYTES(gTextAutoFit);
#undef TEXT_FIT_HASH_BYTES
    return hash;
}

static bool PlutoTextImageHasInk(const Image *image) {
    if (!image || !image->data || image->width <= 0 || image->height <= 0 ||
        image->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) return false;
    const Color *pixels = (const Color *)image->data;
    size_t count = (size_t)image->width * (size_t)image->height;
    for (size_t i = 0; i < count; i++) if (pixels[i].a > 8) return true;
    return false;
}

static bool PlutoTextWriteImage(char *outPath, size_t pathCapacity, bool forceNew) {
    if (!PlutoTextOutputPath(outPath, pathCapacity, forceNew) &&
        !PlutoTextFallbackOutputPath(outPath, pathCapacity)) return false;
    /* The title TGA contains only its text frame, not a transparent 1080p
       canvas. WGPU positions/scales that smaller texture using the matching
       transform props, so editing uploads far fewer pixels. */
    const int referenceWidth = 1920, referenceHeight = 1080;
    float boxW = Clampf(gTextBoxW, .001f, 1.f) * (float)referenceWidth;
    float boxH = Clampf(gTextBoxH, .001f, 1.f) * (float)referenceHeight;
    int layerW = (int)fmaxf(1.f, roundf(boxW));
    int layerH = (int)fmaxf(1.f, roundf(boxH));
    float padX = fmaxf(5.f, boxW * .055f);
    float padY = fmaxf(4.f, boxH * .09f);
    float availableW = fmaxf(1.f, boxW - 2.f * padX);
    float availableH = fmaxf(1.f, boxH - 2.f * padY);

    Font font = PlutoTextFontAt(180);
    int fontSize = (int)Clampf((float)gTextFontSize, 8.f, 180.f);
    char lines[STELLA_TEXT_MAX_LINES][STELLA_TEXT_MAX] = {{0}};
    int lineCount = 1;
    float lineHeight = 0.f, blockHeight = 0.f, maxLineWidth = 0.f;
    if (gTextAutoFit) {
        uint64_t fitSignature = PlutoTextFitSignature();
        if (fitSignature == gTextFitCacheSignature &&
            gTextFitCacheSize >= 8 && gTextFitCacheSize <= 180) {
            fontSize = gTextFitCacheSize;
        } else {
            int low = 8, high = 180, best = 8;
            while (low <= high) {
                int candidateSize = low + (high - low) / 2;
                char candidateLines[STELLA_TEXT_MAX_LINES][STELLA_TEXT_MAX] = {{0}};
                int candidateCount = PlutoTextBuildLines(font, candidateSize, availableW, candidateLines);
                float candidateHeight = (float)candidateSize * 1.18f * (float)candidateCount;
                float candidateWidth = 0.f;
                for (int i = 0; i < candidateCount; i++) {
                    float w = MeasureTextEx(font, candidateLines[i], (float)candidateSize, 0.f).x;
                    if (w > candidateWidth) candidateWidth = w;
                }
                if (candidateWidth <= availableW + .5f && candidateHeight <= availableH + .5f) {
                    best = candidateSize;
                    low = candidateSize + 1;
                } else high = candidateSize - 1;
            }
            fontSize = best;
            gTextFitCacheSignature = fitSignature;
            gTextFitCacheSize = best;
        }
    }
    lineCount = PlutoTextBuildLines(font, fontSize, availableW, lines);
    lineHeight = (float)fontSize * 1.18f;
    blockHeight = (float)lineCount * lineHeight;
    for (int i = 0; i < lineCount; i++) {
        float w = MeasureTextEx(font, lines[i], (float)fontSize, 0.f).x;
        if (w > maxLineWidth) maxLineWidth = w;
    }
    gTextFontSize = fontSize;

    Image textLayer = GenImageColor(layerW, layerH, (Color){0, 0, 0, 0});
    if (!textLayer.data) return false;
    /* Keep glyphs separate from the optional background plate so the
       visibility check below cannot mistake the plate for rendered text. */
    Image glyphLayer = GenImageColor(layerW, layerH, (Color){0, 0, 0, 0});
    Image *glyphTarget = glyphLayer.data ? &glyphLayer : &textLayer;

    float firstY = fmaxf(padY, (boxH - blockHeight) * .5f);
    if (gTextBackground && gTextContent[0]) {
        Rectangle plate = {padX * .5f, firstY - padY * .45f,
                           fmaxf(1.f, fminf(boxW - padX, maxLineWidth + padX)),
                           fminf(boxH - (firstY - padY * .45f), blockHeight + padY * .9f)};
        ImageDrawRectangleRec(&textLayer, plate, gTextAppearance[3]);
    }
    for (int i = 0; i < lineCount && gTextContent[0]; i++) {
        float textWidth = MeasureTextEx(font, lines[i], (float)fontSize, 0.f).x;
        float drawX = gTextAlign == 0 ? padX :
                      gTextAlign == 1 ? (boxW - textWidth) * .5f : boxW - padX - textWidth;
        drawX = Clampf(drawX, 0.f, fmaxf(0.f, boxW - textWidth));
        Vector2 position = {drawX, firstY + (float)i * lineHeight};
        if (gTextShadow) {
            Vector2 shadow = {position.x + 3.f, position.y + 4.f};
            ImageDrawTextEx(glyphTarget, font, lines[i], shadow, (float)fontSize, 0.f, gTextAppearance[2]);
        }
        if (gTextOutline) {
            static const int offsets[8][2] = {{-2,0},{2,0},{0,-2},{0,2},{-2,-2},{2,-2},{-2,2},{2,2}};
            for (int k = 0; k < 8; k++) {
                Vector2 edge = {position.x + (float)offsets[k][0], position.y + (float)offsets[k][1]};
                ImageDrawTextEx(glyphTarget, font, lines[i], edge, (float)fontSize, 0.f, gTextAppearance[1]);
            }
        }
        ImageDrawTextEx(glyphTarget, font, lines[i], position, (float)fontSize, 0.f, gTextAppearance[0]);
    }
    /* Some custom/system font files can exist but fail to produce glyph
       pixels for the active codepoint set. Never save an empty title image: if
       the primary raster pass produced no visible pixels, retry with raylib's
       guaranteed default font and an opaque readable ink color. */
    if (gTextContent[0] && !PlutoTextImageHasInk(glyphTarget)) {
        Font fallbackFont = GetFontDefault();
        Color fallbackInk = gTextAppearance[0];
        if (fallbackInk.a < 96) fallbackInk = (Color){248, 249, 252, 255};
        for (int i = 0; i < lineCount; i++) {
            float textWidth = MeasureTextEx(fallbackFont, lines[i], (float)fontSize, 0.f).x;
            float drawX = gTextAlign == 0 ? padX :
                          gTextAlign == 1 ? (boxW - textWidth) * .5f : boxW - padX - textWidth;
            drawX = Clampf(drawX, 0.f, fmaxf(0.f, boxW - textWidth));
            ImageDrawTextEx(glyphTarget, fallbackFont, lines[i],
                            (Vector2){drawX, firstY + (float)i * lineHeight},
                            (float)fontSize, 0.f, fallbackInk);
        }
    }
    if (glyphTarget == &glyphLayer) {
        /* Keep italic slant subtle and bounded by the internal text padding so
           glyphs never disappear beyond the transparent frame. */
        if (gTextItalic)
            PlutoTextShearGlyphs(&glyphLayer, fminf(padX * .72f, fontSize * .10f), lineHeight, firstY);
        ImageDrawImagePro(&textLayer, glyphLayer,
                          (Rectangle){0.f, 0.f, (float)layerW, (float)layerH},
                          (Rectangle){0.f, 0.f, (float)layerW, (float)layerH},
                          (Vector2){0.f, 0.f}, 0.f, WHITE);
        UnloadImage(glyphLayer);
    }
    /* Underlines are independent decoration: keep them perfectly horizontal
       instead of shearing them with italic glyph pixels. */
    if (gTextUnderline && gTextContent[0]) {
        for (int i = 0; i < lineCount; i++) {
            float textWidth = MeasureTextEx(font, lines[i], (float)fontSize, 0.f).x;
            float drawX = gTextAlign == 0 ? padX :
                          gTextAlign == 1 ? (boxW - textWidth) * .5f : boxW - padX - textWidth;
            drawX = Clampf(drawX, 0.f, fmaxf(0.f, boxW - textWidth));
            Rectangle underline = {drawX, firstY + (float)i * lineHeight + fontSize * 1.02f,
                                    fmaxf(1.f, textWidth), fmaxf(1.5f, fontSize * .045f)};
            ImageDrawRectangleRec(&textLayer, underline, gTextAppearance[0]);
        }
    }
    bool okay = ExportImage(textLayer, outPath);
    if (!okay) {
        /* Keep title creation resilient when the per-user cache folder is
           unavailable (for example, a locked-down or redirected AppData). */
        char fallbackPath[1024];
        if (PlutoTextFallbackOutputPath(fallbackPath, sizeof(fallbackPath)) &&
            ExportImage(textLayer, fallbackPath)) {
            snprintf(outPath, pathCapacity, "%s", fallbackPath);
            okay = true;
        }
    }
    UnloadImage(textLayer);
    return okay;
}

static bool PlutoTextBuildPropsJson(char *out, size_t capacity) {
    char escaped[STELLA_TEXT_MAX * 6 + 16];
    char escapedFontName[640], escapedFontPath[6144], escapedSourceId[512];
    PlutoJsonEscape(gTextContent, escaped, sizeof(escaped));
    PlutoJsonEscape(gTextFontName, escapedFontName, sizeof(escapedFontName));
    PlutoJsonEscape(gTextFontPath, escapedFontPath, sizeof(escapedFontPath));
    PlutoJsonEscape(gTextIsSubtitle ? gTextSubtitleSourceId : "", escapedSourceId, sizeof(escapedSourceId));
    float sourceAspect = (gTextBoxW * 1920.f) / fmaxf(1.f, gTextBoxH * 1080.f);
    const float targetAspect = 1920.f / 1080.f;
    float fitX = 1.f, fitY = 1.f;
    if (sourceAspect > targetAspect) fitY = targetAspect / sourceAspect;
    else fitX = sourceAspect / targetAspect;
    float scaleW = Clampf(gTextBoxW / fmaxf(.001f, fitX) * 100.f, 1.f, 1000.f);
    float scaleH = Clampf(gTextBoxH / fmaxf(.001f, fitY) * 100.f, 1.f, 1000.f);
    float effectiveX = Clampf(gTextPosX, gTextBoxW * .5f, 1.f - gTextBoxW * .5f);
    float effectiveY = Clampf(gTextPosY, gTextBoxH * .5f, 1.f - gTextBoxH * .5f);
    float posX = (effectiveX - .5f) * 1920.f;
    float posY = (effectiveY - .5f) * 1080.f;
    int n = snprintf(out, capacity,
        "{\"isTitle\":true,\"isSubtitle\":%s,\"freePlacement\":%s,\"sourceClipId\":\"%s\",\"content\":\"%s\",\"fontSize\":%d,\"fontFamily\":%d,\"fontName\":\"%s\",\"fontPath\":\"%s\",\"color\":%d,\"align\":%d,"
        "\"bold\":%s,\"italic\":%s,\"underline\":%s,\"outline\":%s,\"shadow\":%s,\"background\":%s,\"autoFit\":%s,"
        "\"x\":%.5f,\"y\":%.5f,\"boxW\":%.5f,\"boxH\":%.5f,"
        "\"layout\":{\"position\":{\"x\":%.5f,\"y\":%.5f},\"scale\":{\"w\":%.5f,\"h\":%.5f}},"
        "\"inkR\":%u,\"inkG\":%u,\"inkB\":%u,\"inkA\":%u,"
        "\"outlineR\":%u,\"outlineG\":%u,\"outlineB\":%u,\"outlineA\":%u,"
        "\"shadowR\":%u,\"shadowG\":%u,\"shadowB\":%u,\"shadowA\":%u,"
        "\"backgroundR\":%u,\"backgroundG\":%u,\"backgroundB\":%u,\"backgroundA\":%u}",
        gTextIsSubtitle ? "true" : "false", gTextIsSubtitle ? "false" : "true",
        escapedSourceId, escaped, gTextFontSize, gTextFontFamily,
        escapedFontName, escapedFontPath, gTextColorIndex, gTextAlign,
        gTextBold ? "true" : "false", gTextItalic ? "true" : "false", gTextUnderline ? "true" : "false",
        gTextOutline ? "true" : "false", gTextShadow ? "true" : "false",
        gTextBackground ? "true" : "false", gTextAutoFit ? "true" : "false",
        (double)gTextPosX, (double)gTextPosY, (double)gTextBoxW, (double)gTextBoxH,
        (double)posX, (double)posY, (double)scaleW, (double)scaleH,
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

static bool PlutoWhisperAppend(char *buffer, size_t capacity, size_t *used, const char *format, ...) {
    if (!buffer || !used || *used >= capacity) return false;
    va_list args;
    va_start(args, format);
    int count = vsnprintf(buffer + *used, capacity - *used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= capacity - *used) return false;
    *used += (size_t)count;
    return true;
}

/* Convert a completed Whisper result into subtitle image clips in one backend
   transaction. Rust replaces all cues from this source and records one undo
   snapshot, so the UI never creates dozens of separate undo entries. */
static void PlutoWhisperApplyTranscriptionResult(void) {
    if (gSubtitleTranscriptionState != 2 || !gSubtitleTranscriptionJson[0]) return;
    const char *jsonEnd = gSubtitleTranscriptionJson + strlen(gSubtitleTranscriptionJson);
    const char *arrayBegin = NULL, *arrayEnd = NULL;
    if (!PlutoJsonMemberRange(gSubtitleTranscriptionJson, jsonEnd, "cues", &arrayBegin, &arrayEnd) ||
        arrayBegin >= arrayEnd || *arrayBegin != '[') {
        gSubtitleTranscriptionState = 4;
        snprintf(gSubtitleTranscriptionDetail, sizeof(gSubtitleTranscriptionDetail), "Whisper returned an invalid cue list.");
        return;
    }
    if (!gSubtitleSourceValid || !gSubtitleSourceClipId[0]) {
        gSubtitleTranscriptionState = 4;
        snprintf(gSubtitleTranscriptionDetail, sizeof(gSubtitleTranscriptionDetail), "The original timeline clip is no longer available.");
        return;
    }

    int replacing = 0;
    for (int i = 0; i < gTimelineClipCount; i++)
        if (gTimelineClips[i].isSubtitle && strcmp(gTimelineClips[i].subtitleSourceId, gSubtitleSourceClipId) == 0) replacing++;
    const int availableSlots = MAX_TIMELINE_CLIPS - gTimelineClipCount + replacing;
    if (availableSlots <= 0) {
        gSubtitleTranscriptionState = 4;
        snprintf(gSubtitleTranscriptionDetail, sizeof(gSubtitleTranscriptionDetail), "The timeline has no room for additional subtitle cues.");
        return;
    }

    const size_t requestCapacity = 16u * 1024u * 1024u;
    char *request = (char *)calloc(1, requestCapacity);
    if (!request) {
        gSubtitleTranscriptionState = 4;
        snprintf(gSubtitleTranscriptionDetail, sizeof(gSubtitleTranscriptionDetail), "Not enough memory to prepare subtitle clips.");
        return;
    }
    char escapedSourceId[512];
    PlutoJsonEscape(gSubtitleSourceClipId, escapedSourceId, sizeof(escapedSourceId));
    size_t used = 0;
    bool okay = PlutoWhisperAppend(request, requestCapacity, &used,
        "{\"command\":\"timeline_replace_subtitles\",\"sourceClipId\":\"%s\",\"clips\":[", escapedSourceId);
    int cueCount = 0;
    const char *scan = arrayBegin + 1;
    const char *limit = arrayEnd - 1;

    /* Preserve editor text settings. Auto-fit mutates gTextFontSize, so reset
       it for every cue and restore the user's inspector values afterward. */
    char savedText[STELLA_TEXT_MAX];
    snprintf(savedText, sizeof(savedText), "%s", gTextContent);
    char savedSubtitleSource[sizeof(gTextSubtitleSourceId)];
    snprintf(savedSubtitleSource, sizeof(savedSubtitleSource), "%s", gTextSubtitleSourceId);
    int savedFontSize = gTextFontSize, savedColorIndex = gTextColorIndex, savedAlign = gTextAlign;
    bool savedBold = gTextBold, savedItalic = gTextItalic, savedUnderline = gTextUnderline;
    bool savedIsSubtitle = gTextIsSubtitle, savedOutline = gTextOutline, savedShadow = gTextShadow;
    bool savedBackground = gTextBackground, savedAutoFit = gTextAutoFit;
    float savedPosX = gTextPosX, savedPosY = gTextPosY, savedBoxW = gTextBoxW, savedBoxH = gTextBoxH;
    Color savedAppearance[4];
    memcpy(savedAppearance, gTextAppearance, sizeof(savedAppearance));

    while (okay && scan < limit && cueCount < availableSlots) {
        while (scan < limit && (*scan == ',' || *scan == ' ' || *scan == '\n' || *scan == '\r' || *scan == '\t')) scan++;
        if (scan >= limit) break;
        if (*scan != '{') { okay = false; break; }
        const char *objectBegin = scan;
        int depth = 0;
        bool inString = false, escaped = false;
        const char *objectEnd = NULL;
        for (; scan < limit; scan++) {
            char c = *scan;
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{') depth++;
            else if (c == '}' && --depth == 0) { objectEnd = scan + 1; scan++; break; }
        }
        if (!objectEnd) { okay = false; break; }
        char cueText[STELLA_TEXT_MAX] = {0};
        double cueStart = 0.0, cueEnd = 0.0;
        if (!PlutoJsonStringRange(objectBegin, objectEnd, "text", cueText, sizeof(cueText)) ||
            !PlutoJsonNumberRange(objectBegin, objectEnd, "start", &cueStart) ||
            !PlutoJsonNumberRange(objectBegin, objectEnd, "end", &cueEnd) ||
            !cueText[0] || !isfinite(cueStart) || !isfinite(cueEnd) || cueEnd <= cueStart) continue;
        cueStart = fmax(0.0, fmin(cueStart, (double)gSubtitleSourceDuration * gSubtitleSourceSpeed));
        cueEnd = fmax(0.0, fmin(cueEnd, (double)gSubtitleSourceDuration * gSubtitleSourceSpeed));
        if (cueEnd <= cueStart + .015) continue;

        snprintf(gTextContent, sizeof(gTextContent), "%s", cueText);
        gTextIsSubtitle = true;
        snprintf(gTextSubtitleSourceId, sizeof(gTextSubtitleSourceId), "%s", gSubtitleSourceClipId);
        gTextFontSize = 58;
        gTextColorIndex = 0;
        gTextAlign = 1;
        gTextBold = true;
        gTextItalic = false;
        gTextUnderline = false;
        gTextOutline = true;
        gTextShadow = true;
        gTextBackground = false;
        gTextAutoFit = true;
        gTextPosX = .5f;
        gTextPosY = .86f;
        gTextBoxW = .86f;
        gTextBoxH = .16f;
        gTextAppearance[0] = (Color){248, 249, 252, 255};
        gTextAppearance[1] = (Color){8, 9, 12, 245};
        gTextAppearance[2] = (Color){0, 0, 0, 205};
        gTextAppearance[3] = (Color){8, 9, 12, 178};

        char imagePath[1024] = {0}, escapedPath[6144], props[8192], clipId[80];
        if (!PlutoTextWriteImage(imagePath, sizeof(imagePath), true) ||
            !PlutoTextBuildPropsJson(props, sizeof(props))) { okay = false; break; }
        PlutoJsonEscape(imagePath, escapedPath, sizeof(escapedPath));
        snprintf(clipId, sizeof(clipId), "subtitle-whisper-%llu", (unsigned long long)gClipSerial++);
        int destinationTrack = TIMELINE_MAX_VIDEO_TRACKS - 1;
        float cueTimelineStart = gSubtitleSourceStart + (float)(cueStart / gSubtitleSourceSpeed);
        float cueTimelineDuration = (float)((cueEnd - cueStart) / gSubtitleSourceSpeed);
        float resolvedStart = cueTimelineStart;
        PlutoResolveTextPlacement(cueTimelineStart, cueTimelineDuration, true, &destinationTrack, &resolvedStart);
        if (cueCount && !PlutoWhisperAppend(request, requestCapacity, &used, ",")) { okay = false; break; }
        if (!PlutoWhisperAppend(request, requestCapacity, &used,
            "{\"id\":\"%s\",\"track\":\"%s\",\"start\":%.6f,\"dur\":%.6f,\"label\":\"Subtitle\",\"path\":\"%s\",\"kind\":\"image\",\"hasAudio\":false,\"sourceOffset\":0,\"sourceDuration\":%.6f,\"props\":{\"text\":%s}}",
            clipId, PlutoTrackName(destinationTrack), (double)resolvedStart,
            (double)fmaxf(.05f, cueTimelineDuration), escapedPath,
            (double)fmaxf(.05f, cueTimelineDuration), props)) { okay = false; break; }
        cueCount++;
    }

    if (okay && scan < limit) {
        const char *remaining = scan;
        while (remaining < limit && (*remaining == ',' || *remaining == ' ' || *remaining == '\n' || *remaining == '\r' || *remaining == '\t')) remaining++;
        if (remaining < limit) {
            okay = false;
            snprintf(gSubtitleTranscriptionDetail, sizeof(gSubtitleTranscriptionDetail), "This transcript has more cues than the current timeline can hold.");
        }
    }

    /* Always restore inspector state, including after failed rasterization. */
    snprintf(gTextContent, sizeof(gTextContent), "%s", savedText);
    snprintf(gTextSubtitleSourceId, sizeof(gTextSubtitleSourceId), "%s", savedSubtitleSource);
    gTextFontSize = savedFontSize;
    gTextColorIndex = savedColorIndex;
    gTextAlign = savedAlign;
    gTextBold = savedBold; gTextItalic = savedItalic; gTextUnderline = savedUnderline;
    gTextIsSubtitle = savedIsSubtitle; gTextOutline = savedOutline; gTextShadow = savedShadow;
    gTextBackground = savedBackground; gTextAutoFit = savedAutoFit;
    gTextPosX = savedPosX; gTextPosY = savedPosY; gTextBoxW = savedBoxW; gTextBoxH = savedBoxH;
    memcpy(gTextAppearance, savedAppearance, sizeof(savedAppearance));

    if (okay && cueCount > 0)
        okay = PlutoWhisperAppend(request, requestCapacity, &used, "]}");
    else if (cueCount == 0 && okay) okay = false;
    if (okay) okay = PlutoTimelineCall(request);
    free(request);
    if (okay) {
        gPreviewDirty = true;
        gPreviewDebounce = 0.f;
        snprintf(gBackendMessage, sizeof(gBackendMessage), "Generated %d subtitle cues", cueCount);
        gSubtitleTranscriptionState = 0;
        gSubtitleTranscriptionJson[0] = 0;
        gSubtitleTranscribePopupOpen = false;
    } else {
        if (!gBackendMessage[0]) snprintf(gBackendMessage, sizeof(gBackendMessage), "Could not insert generated subtitle cues");
        snprintf(gSubtitleTranscriptionDetail, sizeof(gSubtitleTranscriptionDetail), "%.255s", gBackendMessage);
        gSubtitleTranscriptionState = 4;
    }
}

static bool PlutoTextApply(bool forceNew, bool liveUpdate) {
    if (forceNew && !gTextContent[0]) {
        strcpy(gBackendMessage, "Enter some text first");
        return false;
    }
    char path[1024], props[8192];
    const uint64_t rasterSignature = PlutoTextRasterSignature();
    const bool reuseRaster = !forceNew && gTextHasCroppedAsset && gTextOutputPath[0] &&
        FileExists(gTextOutputPath) && rasterSignature == gTextRenderedSignature;
    if (reuseRaster) {
        snprintf(path, sizeof(path), "%s", gTextOutputPath);
    } else {
        /* Render first: auto-fit updates gTextFontSize, which must be saved in the
           clip properties alongside the generated image. */
        if (!PlutoTextWriteImage(path, sizeof(path), forceNew)) {
            strcpy(gBackendMessage, "Could not create the title image");
            return false;
        }
        snprintf(gTextOutputPath, sizeof(gTextOutputPath), "%s", path);
        gTextHasCroppedAsset = true;
        gTextRenderedSignature = PlutoTextRasterSignature();
    }
    if (!PlutoTextBuildPropsJson(props, sizeof(props))) {
        strcpy(gBackendMessage, "Could not create the title properties");
        return false;
    }
    bool okay;
    if (forceNew) okay = PlutoTimelineAddTitleClip(path, props, gPlayhead, gTextDuration);
    else if (gTextTitleSelected && gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount)
        okay = PlutoTimelineUpdateTitleClip(gTimelineClips[gTimelineSelected].id, path, props, liveUpdate);
    else return false;
    if (okay) {
        if (!liveUpdate) { gTextInputFocused = false; gTextFocusStartedAt = 0.0; }
        gRightTab = 3;
        if (!liveUpdate) strcpy(gBackendMessage, forceNew ? "Text box added to the timeline" : "Title updated");
    }
    return okay;
}

static void PlutoTextCreateBox(float x, float y, float width, float height) {
    /* Preserve edits on the previously selected title before changing the box
       geometry for a newly dragged text frame. */
    PlutoTextAutoUpdateFlush();
    PlutoTimelineEndPropertyEdit();
    gTextIsSubtitle = false;
    gTextSubtitleSourceId[0] = 0;
    width = Clampf(width, .001f, 1.f);
    height = Clampf(height, .001f, 1.f);
    gTextBoxW = width;
    gTextBoxH = height;
    gTextPosX = Clampf(x, width * .5f, 1.f - width * .5f);
    gTextPosY = Clampf(y, height * .5f, 1.f - height * .5f);
    gTextAutoFit = true;
    gTextFontSize = 180;
    if (!gTextContent[0]) strcpy(gTextContent, "Text");
    gTextInputFocused = false;
    gTextFocusStartedAt = 0.0;
    if (!PlutoTextApply(true, false) && !gBackendMessage[0])
        strcpy(gBackendMessage, "Could not place the text clip on V6");
}

static uint64_t PlutoTextRasterSignature(void) {
    uint64_t hash = 1469598103934665603ull;
#define TEXT_RASTER_HASH_BYTES(value) do { \
    const unsigned char *bytes = (const unsigned char *)&(value); \
    for (size_t byteIndex = 0; byteIndex < sizeof(value); byteIndex++) { hash ^= bytes[byteIndex]; hash *= 1099511628211ull; } \
} while (0)
    const unsigned char *textBytes = (const unsigned char *)gTextContent;
    for (size_t byteIndex = 0; byteIndex <= strlen(gTextContent); byteIndex++) {
        hash ^= textBytes[byteIndex]; hash *= 1099511628211ull;
    }
    TEXT_RASTER_HASH_BYTES(gTextFontSize);
    TEXT_RASTER_HASH_BYTES(gTextAppearance);
    TEXT_RASTER_HASH_BYTES(gTextAlign);
    TEXT_RASTER_HASH_BYTES(gTextBold);
    TEXT_RASTER_HASH_BYTES(gTextItalic);
    TEXT_RASTER_HASH_BYTES(gTextUnderline);
    TEXT_RASTER_HASH_BYTES(gTextFontFamily);
    for (size_t fontByte = 0; fontByte < sizeof(gTextFontPath); fontByte++) { hash ^= (unsigned char)gTextFontPath[fontByte]; hash *= 1099511628211ull; }
    TEXT_RASTER_HASH_BYTES(gTextOutline);
    TEXT_RASTER_HASH_BYTES(gTextShadow);
    TEXT_RASTER_HASH_BYTES(gTextBackground);
    TEXT_RASTER_HASH_BYTES(gTextAutoFit);
    TEXT_RASTER_HASH_BYTES(gTextBoxW);
    TEXT_RASTER_HASH_BYTES(gTextBoxH);
#undef TEXT_RASTER_HASH_BYTES
    return hash;
}

static uint64_t PlutoTextStateSignature(void) {
    uint64_t hash = 1469598103934665603ull;
#define TEXT_HASH_BYTES(value) do { \
    const unsigned char *bytes = (const unsigned char *)&(value); \
    for (size_t byteIndex = 0; byteIndex < sizeof(value); byteIndex++) { hash ^= bytes[byteIndex]; hash *= 1099511628211ull; } \
} while (0)
    const unsigned char *textBytes = (const unsigned char *)gTextContent;
    for (size_t byteIndex = 0; byteIndex <= strlen(gTextContent); byteIndex++) {
        hash ^= textBytes[byteIndex]; hash *= 1099511628211ull;
    }
    TEXT_HASH_BYTES(gTextFontSize);
    TEXT_HASH_BYTES(gTextColorIndex);
    TEXT_HASH_BYTES(gTextAppearance);
    TEXT_HASH_BYTES(gTextAlign);
    TEXT_HASH_BYTES(gTextBold);
    TEXT_HASH_BYTES(gTextItalic);
    TEXT_HASH_BYTES(gTextUnderline);
    TEXT_HASH_BYTES(gTextFontFamily);
    for (size_t fontByte = 0; fontByte < sizeof(gTextFontPath); fontByte++) { hash ^= (unsigned char)gTextFontPath[fontByte]; hash *= 1099511628211ull; }
    for (size_t nameByte = 0; nameByte < sizeof(gTextFontName); nameByte++) { hash ^= (unsigned char)gTextFontName[nameByte]; hash *= 1099511628211ull; }
    TEXT_HASH_BYTES(gTextIsSubtitle);
    TEXT_HASH_BYTES(gTextOutline);
    TEXT_HASH_BYTES(gTextShadow);
    TEXT_HASH_BYTES(gTextBackground);
    TEXT_HASH_BYTES(gTextAutoFit);
    TEXT_HASH_BYTES(gTextPosX);
    TEXT_HASH_BYTES(gTextPosY);
    TEXT_HASH_BYTES(gTextBoxW);
    TEXT_HASH_BYTES(gTextBoxH);
#undef TEXT_HASH_BYTES
    return hash;
}

static void PlutoTextAutoUpdateObserve(void) {
    char currentClip[64] = {0};
    if (gTextTitleSelected && gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount)
        strncpy(currentClip, gTimelineClips[gTimelineSelected].id, sizeof(currentClip) - 1);
    uint64_t signature = PlutoTextStateSignature();
    if (!gTextAutoObserverInitialized || strcmp(currentClip, gTextAutoObservedClipId) != 0) {
        strncpy(gTextAutoObservedClipId, currentClip, sizeof(gTextAutoObservedClipId) - 1);
        gTextAutoObservedClipId[sizeof(gTextAutoObservedClipId) - 1] = 0;
        gTextAutoObservedSignature = signature;
        gTextAutoObserverInitialized = true;
        return;
    }
    if (currentClip[0] && signature != gTextAutoObservedSignature) {
        gTextAutoObservedSignature = signature;
        gTextAutoUpdatePending = true;
        gTextAutoUpdateChangedAt = GetTime();
        strncpy(gTextAutoUpdateClipId, currentClip, sizeof(gTextAutoUpdateClipId) - 1);
        gTextAutoUpdateClipId[sizeof(gTextAutoUpdateClipId) - 1] = 0;
    }
}

static void PlutoTextAutoUpdateFlush(void) {
    if (!gTextAutoUpdatePending) return;
    bool sameClip = gTextTitleSelected && gTimelineSelected >= 0 &&
        gTimelineSelected < gTimelineClipCount &&
        strcmp(gTimelineClips[gTimelineSelected].id, gTextAutoUpdateClipId) == 0;
    gTextAutoUpdatePending = false;
    if (sameClip && PlutoTextApply(false, true)) {
        double now = GetTime();
        gTextAutoObservedSignature = PlutoTextStateSignature();
        gTextAutoUpdateChangedAt = now;
        gTextAutoUpdateLastFlushAt = now;
        /* Consecutive updates inside one editing burst share one undo step. */
        gTextAutoUpdateCommitAt = now + .65;
    }
}

static void PlutoTextAutoUpdateTick(void) {
    double now = GetTime();
    /* Keep live title edits responsive at up to 20 updates/sec. The preview
       worker still has a single in-flight request, so intermediate edits are
       naturally coalesced instead of building an unbounded queue. */
    if (gTextAutoUpdatePending &&
        (gTextAutoUpdateLastFlushAt <= 0.0 || now - gTextAutoUpdateLastFlushAt >= .05))
        PlutoTextAutoUpdateFlush();
    if (gTimelinePropertyEditOpen && gTextAutoUpdateCommitAt > 0.0 && now >= gTextAutoUpdateCommitAt) {
        PlutoTimelineEndPropertyEdit();
        gTextAutoUpdateCommitAt = 0.0;
        gTextAutoUpdateLastFlushAt = 0.0;
    }
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

static int gTextColorEditIndex = -1;
static bool gTextColorEditorOpen;
static bool gTextColorHexReplaceOnType;
static bool gTextColorHexInvalid;
static char gTextColorHex[10] = "#F5F6F9";
static void TextStyleToggle(const char *idName, const char *label, bool *value);
static void TextColorHexLoad(int index) {
    if (index < 0 || index >= 4) return;
    Color c = gTextAppearance[index];
    snprintf(gTextColorHex, sizeof(gTextColorHex), "#%02X%02X%02X",
             (unsigned)c.r, (unsigned)c.g, (unsigned)c.b);
    gTextWheelPuck[index] = gTextWheelPuckT[index] = PlutoTextColorToWheel(c);
    gTextColorHexInvalid = false;
}

static int TextColorHexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool TextColorHexParse(const char *text, int index, Color *out) {
    if (!text || !out || index < 0 || index >= 4) return false;
    const char *hex = text;
    if (*hex == '#') hex++;
    size_t length = strlen(hex);
    if (length != 6 && length != 8) return false;
    unsigned channels[4] = {0, 0, 0, gTextAppearance[index].a};
    for (size_t i = 0; i < length / 2; i++) {
        int hi = TextColorHexNibble(hex[i * 2]);
        int lo = TextColorHexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        channels[i] = (unsigned)(hi * 16 + lo);
    }
    *out = (Color){(unsigned char)channels[0], (unsigned char)channels[1],
                   (unsigned char)channels[2], (unsigned char)channels[3]};
    return true;
}

static void TextColorHexApply(int index) {
    Color parsed;
    if (TextColorHexParse(gTextColorHex, index, &parsed)) {
        gTextAppearance[index] = parsed;
        gTextWheelPuck[index] = gTextWheelPuckT[index] = PlutoTextColorToWheel(parsed);
        gTextColorEditorOpen = false;
        gTextColorHexFocused = false;
        gTextColorHexInvalid = false;
    } else {
        gTextColorHexInvalid = true;
    }
}

static void TextColorHexEditor(int index) {
    Clay_ElementId field = CLAY_IDI("TextColorHexField", index);
    Bs fb = Interact(field);
    if (fb.clicked) {
        gTextColorHexFocused = true;
        gTextColorHexReplaceOnType = true;
        gTextInputFocused = false;
        gTextFocusStartedAt = 0.0;
    }
    if (gTextColorHexFocused) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            gTextColorHexFocused = false;
            gTextColorHexInvalid = false;
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            if (gTextColorHexReplaceOnType) {
                gTextColorHex[0] = 0;
                gTextColorHexReplaceOnType = false;
            } else {
                size_t n = strlen(gTextColorHex);
                if (n > 0) gTextColorHex[n - 1] = 0;
            }
            gTextColorHexInvalid = false;
        }
        int cp;
        while ((cp = GetCharPressed()) != 0) {
            char ch = (char)cp;
            bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                         (ch >= 'A' && ch <= 'F');
            bool isHash = ch == '#' && (gTextColorHex[0] == 0 || gTextColorHexReplaceOnType);
            if (!isHex && !isHash) continue;
            if (gTextColorHexReplaceOnType) {
                gTextColorHex[0] = 0;
                gTextColorHexReplaceOnType = false;
            }
            size_t n = strlen(gTextColorHex);
            if (n + 1 < sizeof(gTextColorHex)) {
                gTextColorHex[n] = ch;
                gTextColorHex[n + 1] = 0;
                gTextColorHexInvalid = false;
            }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) TextColorHexApply(index);
    }

    CLAY(CLAY_IDI("TextColorEditorBody", index), {.layout = {COLM, .sizing = {GROW, FIX(index == 0 ? 96 : 132)}, .childGap = 7}}) {
        if (index == 1) TextStyleToggle("TextOutline", "Outline", &gTextOutline);
        else if (index == 2) TextStyleToggle("TextShadow", "Shadow", &gTextShadow);
        else if (index == 3) TextStyleToggle("TextBackground", "Background", &gTextBackground);
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(82)}, .childGap = 10, .childAlignment = AL_Y}}) {
        Clay_ElementId wheel = CLAY_IDI("TextColorEditorWheel", index);
        Bs wb = Interact(wheel);
        if (gTextColorEditorOpen && gTextColorEditIndex == index)
            TextWheelLogic(index, wheel);
        if (gTextWheelDrag[index]) {
            Color c = gTextAppearance[index];
            snprintf(gTextColorHex, sizeof(gTextColorHex), "#%02X%02X%02X",
                     (unsigned)c.r, (unsigned)c.g, (unsigned)c.b);
            gTextColorHexInvalid = false;
        }
        Vector2 puck = gTextWheelPuck[index];
        Color color = gTextAppearance[index];
        CLAY(wheel, {.layout = {.sizing = {FIX(82), FIX(82)}},
                     .custom = {.customData = NC(K_WHEEL, C_CARD,
                         (Clay_Color){puck.x, puck.y, 0, 0},
                         (Clay_Color){color.r, color.g, color.b, color.a},
                         1.f, fmaxf(wb.hover * .6f, gTextWheelDrag[index] ? 1.f : 0.f), 1)}}) {}

        CLAY(CLAY_IDI("TextColorEditorHexColumn", index), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
            Tx("Hex color", 13, C_MUTED, 0);
            CLAY(CLAY_IDI("TextColorEditorHexRow", index), {.layout = {.sizing = {GROW, FIX(34)}, .childGap = 7, .childAlignment = AL_Y}}) {
                CLAY(field, {.layout = {.sizing = {GROW, FIX(34)}, .padding = {9, 5, 0, 0}, .childAlignment = AL_Y},
                             .backgroundColor = C_RFIELD,
                             .cornerRadius = CLAY_CORNER_RADIUS(9),
                             .border = {.color = gTextColorHexInvalid ? RGB(190, 78, 83) :
                                        Mix(C_RCARDB, RGB(100, 104, 114), (fb.hover + (gTextColorHexFocused ? .7f : 0.f)) * .4f),
                                        .width = CLAY_BORDER_OUTSIDE(1)}}) {
                    Tx(gTextColorHex[0] ? gTextColorHex : "#RRGGBB", 14,
                       gTextColorHexInvalid ? RGB(238, 126, 130) : C_TEXT, 0);
                }
                Clay_ElementId applyId = CLAY_IDI("TextColorHexApply", index);
                Bs ab = Interact(applyId);
                CLAY(applyId, {.layout = {.sizing = {FIX(58), FIX(34)}, .childAlignment = AL_C},
                               .backgroundColor = BtnBg(C_CARD, RGB(48, 50, 57), ab),
                               .cornerRadius = CLAY_CORNER_RADIUS(9)}) {
                    Tx("Apply", 13, C_TEXT, 0);
                }
                if (ab.clicked) TextColorHexApply(index);
            }
            Tx(gTextColorHexInvalid ? "Use #RRGGBB or #RRGGBBAA" : "Drag the wheel or enter a hex code",
               11.5f, gTextColorHexInvalid ? RGB(210, 118, 122) : C_MUTED, 0);
        }
        }
    }
}

static double gTextColorLastClickAt[4];

static void TextColorRow(int index, const char *label, float editorT) {
    Clay_ElementId row = CLAY_IDI("TextColorLabel", index);
    CLAY(row, {.layout = {.sizing = {GROW, FIX(34)}, .childAlignment = AL_Y}}) {
        Tx(label, 15, C_DIM, 0);
        HSPACE
        Clay_ElementId swatch = CLAY_IDI("TextColorSwatch", index);
        Bs b = Interact(swatch);
        if (b.clicked) {
            double now = GetTime();
            bool doubleClick = gTextColorLastClickAt[index] > 0.0 &&
                               now - gTextColorLastClickAt[index] <= .42;
            gTextColorLastClickAt[index] = doubleClick ? 0.0 : now;
            if (doubleClick) {
                /* A deliberate double-click expands the editor; single clicks
                   no longer make the inspector jump while selecting a row. */
                if (gTextColorEditIndex == index && gTextColorEditorOpen) {
                    gTextColorEditorOpen = false;
                    gTextColorHexFocused = false;
                    gTextColorHexInvalid = false;
                } else {
                    gTextColorEditIndex = index;
                    TextColorHexLoad(index);
                    gTextColorHexFocused = false;
                    gTextColorHexReplaceOnType = false;
                    gTextColorEditorOpen = true;
                }
                gTextInputFocused = false;
                gTextFocusStartedAt = 0.0;
            }
        }
        Color color = gTextAppearance[index];
        CLAY(swatch, {.layout = {.sizing = {FIX(48), FIX(12)}},
                      .backgroundColor = RGB(47, 49, 55),
                      .cornerRadius = CLAY_CORNER_RADIUS(7),
                      .border = {.color = Mix(RGB(86, 89, 98), RGB(180, 184, 194), b.hover * .5f),
                                 .width = CLAY_BORDER_OUTSIDE(1)}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(1)},
                          .backgroundColor = (Clay_Color){color.r, color.g, color.b, color.a},
                          .cornerRadius = CLAY_CORNER_RADIUS(6)}) {}
        }
    }
    if (gTextColorEditIndex == index && editorT > .004f) {
        float editorHeight = (index == 0 ? 96.f : 132.f) * editorT;
        CLAY(CLAY_IDI("TextColorEditorWrap", index),
             {.layout = {COLM, .sizing = {GROW, CLAY_SIZING_FIXED(editorHeight)}},
              .clip = {.horizontal = true, .vertical = true}}) {
            TextColorHexEditor(index);
        }
    }
}

/* Compact segmented control retained for text alignment. */
static bool TextChoiceButton(Clay_ElementId id, const char *label, bool on, bool dot) {
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(34)}, .padding = {8, 8, 0, 0},
                          .childGap = 7, .childAlignment = AL_C},
              .backgroundColor = on ? RGB(52, 54, 62) : BtnBg(C_RFIELD, RGB(46, 47, 54), b),
              .cornerRadius = CLAY_CORNER_RADIUS(10),
              .border = {.color = Mix(on ? RGB(92, 96, 106) : C_RCARDB,
                                      RGB(84, 87, 96), b.hover * .5f),
                         .width = CLAY_BORDER_OUTSIDE(1)}}) {
        if (dot)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(6), FIX(6)}},
                          .backgroundColor = on ? C_TEXT : RGB(74, 77, 85),
                          .cornerRadius = CLAY_CORNER_RADIUS(3)}) {}
        Tx(label, 14, on ? C_TEXT : C_DIM, 0);
    }
    return b.clicked;
}

/* Unboxed inspector row: descriptive label on the left, switch on the right. */
static void TextStyleToggle(const char *idName, const char *label, bool *value) {
    Clay_ElementId id = TextDynamicElementId(idName);
    Bs b = Interact(id);
    if (b.clicked) *value = !*value;
    float on = Anim(id.id + 0x5a31u, *value ? 1.f : 0.f, 22.f);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(34)}, .childAlignment = AL_Y}}) {
        Tx(label, 15, *value ? C_TEXT : C_DIM, 0);
        HSPACE
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(42), FIX(24)}},
                      .custom = {.customData = NC(K_SWITCH, C_CARD, C_CARD, C_CARD, on,
                                                  b.hover * .55f, 0)}}) {}
    }
}

static void TextColorEditorReset(void) {
    gTextColorEditorOpen = false;
    gTextColorEditIndex = -1;
    gTextColorHexFocused = false;
    gTextColorHexInvalid = false;
    memset(gTextColorLastClickAt, 0, sizeof(gTextColorLastClickAt));
    float *progress = AnimPtr(CLAY_ID("TextColorEditorAnim").id ^ 0x5BD1E995u, 0.f);
    if (progress) *progress = 0.f;
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
                if (gEditValueId == scrubId.id) EditCaretText(22, C_TEXT);
                else Tx(Fmt("%d", (int)roundf(pct)), 22, C_TEXT, 0);
                Tx("%", 17, C_ICON, 0);
            }
            GAPX(3)
            CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}}}) {
                StellaArrowAction up = Arrow(CLAY_IDI("TxtPosU", n), I_CHEV_UP);
                StellaArrowAction down = Arrow(CLAY_IDI("TxtPosD", n), I_CHEV_DN);
                if (up.step) *norm = fminf(hi, *norm + .01f);
                if (down.step) *norm = fmaxf(lo, *norm - .01f);
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

static void DrawTextFontMenuOverlay(void) {
    if (!gTextFontMenuOpen || gTextFontMenuRect.width <= 0.f) return;
    Rectangle menu = gTextFontMenuRect;
    Vector2 mouse = GetMousePosition();
    const float rowHeight = 29.f, innerTop = menu.y + 4.f;
    FillRR(menu, 10.f, (Color){25, 26, 30, 252});
    StrokeRR(menu, 10.f, 1.f, (Color){64, 67, 76, 255});
    int totalRows = gSystemFontCount + 1;
    float contentHeight = (float)totalRows * rowHeight;
    float maxScroll = fmaxf(0.f, contentHeight - (menu.height - 8.f));
    gTextFontMenuScroll = Clampf(gTextFontMenuScroll, 0.f, maxScroll);
    BeginScissorMode((int)menu.x + 3, (int)menu.y + 3, (int)menu.width - 6, (int)menu.height - 6);
    for (int rowIndex = 0; rowIndex < totalRows; rowIndex++) {
        float y = innerTop + (float)rowIndex * rowHeight - gTextFontMenuScroll;
        if (y + rowHeight < menu.y || y > menu.y + menu.height) continue;
        Rectangle row = {menu.x + 4.f, y, menu.width - 8.f, rowHeight};
        bool isSelected = rowIndex == 0 ? (!gTextFontPath[0] && gTextFontFamily == 0) :
            (gTextFontPath[0] && strcmp(gTextFontPath, gSystemFonts[rowIndex - 1].path) == 0);
        bool hover = CheckCollisionPointRec(mouse, row);
        if (hover || isSelected)
            FillRR(row, 7.f, isSelected ? (Color){51, 54, 62, 255} : (Color){39, 41, 47, 255});
        const char *name = rowIndex == 0 ? "System default" : gSystemFonts[rowIndex - 1].name;
        TxtAt(0, name, row.x + 11.f, row.y + 7.f, 13.f,
              isSelected ? (Color){242, 243, 246, 255} : (Color){184, 187, 195, 255});
        if (isSelected) TxtAt(0, "✓", row.x + row.width - 22.f, row.y + 7.f, 13.f, (Color){228, 230, 235, 255});
    }
    EndScissorMode();
}

static void DrawSubtitleTranscribePopup(void) {
    if (!gSubtitleTranscribePopupOpen) return;
    const int selected = gSubtitleSelectedTier < 0 || gSubtitleSelectedTier > 2 ? 1 : gSubtitleSelectedTier;
    static const char *tierNames[3] = {"Performance", "Balanced", "Quality"};
    static const char *tierSpecs[3] = {"Base Q8_0 · fastest · 78 MiB", "Small Q8_0 · balanced · 252 MiB", "Medium Q5_0 · best accuracy · 514 MiB"};
    const float width = fminf(560.f, (float)GetScreenWidth() - 36.f);
    const float height = 390.f;
    Rectangle card = {(GetScreenWidth() - width) * .5f, (GetScreenHeight() - height) * .5f, width, height};
    Vector2 mouse = GetMousePosition();
    Rectangle closeRect = {card.x + card.width - 43.f, card.y + 15.f, 28.f, 28.f};
    Rectangle toggle = {card.x + 24.f, card.y + 104.f, card.width - 48.f, 58.f};
    float segmentWidth = toggle.width / 3.f;
    FillRR((Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()}, 0.f, (Color){0, 0, 0, 166});
    FillRR(card, 17.f, (Color){25, 26, 30, 255});
    StrokeRR(card, 17.f, 1.f, (Color){65, 68, 77, 255});
    TxtAt(0, "Generate subtitles", card.x + 25.f, card.y + 22.f, 20.f, (Color){246, 247, 250, 255});
    TxtAt(0, "Choose a transcription quality · up to 30 minutes per clip", card.x + 25.f, card.y + 54.f, 12.f, (Color){158, 162, 172, 255});
    FillRR(closeRect, 8.f, CheckCollisionPointRec(mouse, closeRect) ? (Color){57, 59, 67, 255} : (Color){38, 39, 44, 255});
    TxtAt(0, "×", closeRect.x + 9.f, closeRect.y + 3.f, 20.f, (Color){205, 208, 215, 255});
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, closeRect)) {
        gSubtitleTranscribePopupOpen = false;
        return;
    }
    for (int i = 0; i < 3; i++) {
        Rectangle segment = {toggle.x + segmentWidth * i, toggle.y, segmentWidth, toggle.height};
        bool active = i == selected;
        bool hover = CheckCollisionPointRec(mouse, segment);
        FillRR(segment, i == 0 || i == 2 ? 8.f : 0.f,
               active ? (Color){59, 82, 121, 255} : hover ? (Color){43, 45, 52, 255} : (Color){32, 33, 38, 255});
        if (active) StrokeRR(segment, i == 0 || i == 2 ? 8.f : 0.f, 1.3f, (Color){112, 158, 232, 255});
        TxtAt(0, tierNames[i], segment.x + 9.f, segment.y + 12.f, 12.5f,
              active ? (Color){250, 251, 255, 255} : (Color){190, 193, 201, 255});
        if (active) TxtAt(0, "SELECTED", segment.x + 9.f, segment.y + 34.f, 8.5f, (Color){190, 211, 245, 255});
        else TxtAt(0, i == 0 ? "FAST" : i == 1 ? "RECOMMENDED" : "ACCURACY", segment.x + 9.f, segment.y + 34.f, 8.5f, (Color){125, 129, 139, 255});
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover) gSubtitleSelectedTier = i;
    }
    /* Re-read the selected value after processing a segment click. */
    const int tier = gSubtitleSelectedTier < 0 || gSubtitleSelectedTier > 2 ? 1 : gSubtitleSelectedTier;
    StellaWhisperModelUi *model = &gWhisperModels[tier];
    TxtAt(0, tierSpecs[tier], card.x + 27.f, card.y + 178.f, 11.f, (Color){166, 169, 178, 255});
    if (gSubtitleSourceValid) {
        char sourceLine[320];
        snprintf(sourceLine, sizeof(sourceLine), "Selected audio: %.42s", gSubtitleSourceLabel[0] ? gSubtitleSourceLabel : "Timeline clip");
        TxtAt(0, sourceLine, card.x + 27.f, card.y + 201.f, 11.f, (Color){200, 203, 211, 255});
    } else {
        TxtAt(0, "Select an audio clip or a video clip with audio on the timeline.", card.x + 27.f, card.y + 201.f, 10.5f, (Color){220, 163, 137, 255});
    }

    Rectangle info = {card.x + 24.f, card.y + 228.f, card.width - 48.f, 46.f};
    FillRR(info, 9.f, (Color){19, 20, 23, 255});
    const char *message = model->downloading ? (model->state == 2 ? "Verifying downloaded model…" : "Downloading model…") :
        model->installed ? "Model is installed and ready for offline transcription." :
        "This model is optional and must be downloaded before it can be used.";
    TxtAt(0, message, info.x + 12.f, info.y + 15.f, 10.5f,
          model->installed ? (Color){151, 210, 169, 255} : (Color){178, 181, 190, 255});

    Rectangle action = {card.x + 24.f, card.y + 291.f, card.width - 48.f, 48.f};
    bool transcribing = gSubtitleTranscriptionState == 1 && gSubtitleTranscriptionRequestId != 0;
    bool busy = model->downloading || transcribing;
    bool canTranscribe = model->installed && gSubtitleSourceValid && !busy;
    Color actionColor = busy ? (Color){71, 49, 49, 255} : canTranscribe ? (Color){57, 91, 143, 255} :
                        model->installed ? (Color){46, 48, 54, 255} : (Color){52, 75, 112, 255};
    FillRR(action, 10.f, CheckCollisionPointRec(mouse, action) ? (Color){(unsigned char)fminf(255, actionColor.r + 12), (unsigned char)fminf(255, actionColor.g + 12), (unsigned char)fminf(255, actionColor.b + 12), 255} : actionColor);
    StrokeRR(action, 10.f, 1.f, (Color){82, 88, 100, 255});
    char actionText[120];
    if (transcribing) snprintf(actionText, sizeof(actionText), "Cancel transcription");
    else if (model->downloading) snprintf(actionText, sizeof(actionText), "Cancel download");
    else if (model->installed && gSubtitleSourceValid) snprintf(actionText, sizeof(actionText), "Transcribe selected audio");
    else if (model->installed) snprintf(actionText, sizeof(actionText), "Select an audio clip first");
    else snprintf(actionText, sizeof(actionText), "Download %s model · %s", tierNames[tier], tier == 0 ? "78 MiB" : tier == 1 ? "252 MiB" : "514 MiB");
    Vector2 textSize = MeasureTextEx(GetFontDefault(), actionText, 13.f, 1.f);
    TxtAt(0, actionText, action.x + (action.width - textSize.x) * .5f, action.y + 16.f, 13.f,
          (model->installed && !gSubtitleSourceValid && !busy) ? (Color){137, 140, 149, 255} : (Color){245, 247, 252, 255});

    if (busy) {
        float progress = transcribing ? gSubtitleTranscriptionProgress : model->progress;
        Rectangle bar = {card.x + 25.f, card.y + 347.f, card.width - 50.f, 4.f};
        FillRR(bar, 2.f, (Color){48, 50, 57, 255});
        FillRR((Rectangle){bar.x, bar.y, bar.width * fminf(1.f, fmaxf(0.f, progress)), bar.height}, 2.f, (Color){111, 160, 236, 255});
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, action)) {
        if (transcribing) PlutoWhisperTranscribeCancel();
        else if (model->downloading) PlutoWhisperModelCancel(tier);
        else if (model->installed && gSubtitleSourceValid) {
            if (!PlutoWhisperTranscribeStart(tier)) {
                gSubtitleTranscriptionState = 4;
                snprintf(gSubtitleTranscriptionDetail, sizeof(gSubtitleTranscriptionDetail), "Could not start transcription. Check the selected clip and model.");
            }
        } else if (!model->installed) PlutoWhisperModelStart(tier);
    }

    const char *status = transcribing ? gSubtitleTranscriptionDetail : model->downloading ? model->detail :
        model->state == 5 ? model->detail :
        (gSubtitleTranscriptionState == 4 || gSubtitleTranscriptionState == 3) ? gSubtitleTranscriptionDetail : NULL;
    if (status && status[0]) TxtAt(0, status, card.x + 27.f, card.y + 360.f, 9.5f, (Color){167, 171, 181, 255});
    if (gSubtitleTranscriptionState == 4 && gSubtitleTranscriptionDetail[0])
        TxtAt(0, gSubtitleTranscriptionDetail, card.x + 27.f, card.y + 360.f, 9.f, (Color){225, 154, 139, 255});
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, card) && GetTime() - gSubtitlePopupOpenedAt > .12)
        gSubtitleTranscribePopupOpen = false;
    if (IsKeyPressed(KEY_ESCAPE)) gSubtitleTranscribePopupOpen = false;
}

static void TextView(void) {
    /* A selected title owns its own open colour editor. Do not carry an open
       wheel across clips when the user changes selection in the timeline. */
    static char colorEditorClipId[64] = {0};
    char selectedClipId[64] = {0};
    if (gTextTitleSelected && gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount)
        snprintf(selectedClipId, sizeof(selectedClipId), "%s", gTimelineClips[gTimelineSelected].id);
    if (strcmp(colorEditorClipId, selectedClipId) != 0) {
        snprintf(colorEditorClipId, sizeof(colorEditorClipId), "%s", selectedClipId);
        TextColorEditorReset();
    }

    /* Model selection, downloading, and transcription live in the + popup.
       Generated captions are inserted as normal subtitle clips on the S lane. */
    int subtitleCount = 0;
    for (int i = 0; i < gTimelineClipCount; i++)
        if (gTimelineClips[i].isSubtitle) subtitleCount++;
    CLAY(CLAY_ID("SubtitleCard"), {.layout = {COLM, .sizing = {GROW, FIT}},
                                   .backgroundColor = C_RCARD,
                                   .cornerRadius = CLAY_CORNER_RADIUS(16),
                                   .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Clay_ElementId subtitleHeader = CLAY_ID("SubtitleHeader");
        if (EffectsSectionHeader(subtitleHeader, "Subtitles", I_NOTE,
                                 gSubtitleCardOpen, false, true))
            gSubtitleCardOpen = !gSubtitleCardOpen;
        if (gSubtitleCardOpen) {
            CLAY(CLAY_ID("SubtitleEmptyState"), {.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = {GROW, FIX(78)},
                                                         .padding = {12, 12, 12, 12}, .childAlignment = AL_Y, .childGap = 9},
                                                  .backgroundColor = C_RCARD,
                                                  .cornerRadius = {0.f, 0.f, 16.f, 16.f}}) {
                CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 4}}) {
                    Tx(subtitleCount ? Fmt("%d subtitle cues", subtitleCount) : "No subtitles yet", 13.5f, C_TEXT, 0);
                    Tx(subtitleCount ? "Existing captions on the timeline" : "Select audio and press + to generate subtitles", 11.5f, C_MUTED, 0);
                }
                HSPACE
                Clay_ElementId addSubtitle = CLAY_ID("SubtitleGenerateButton");
                Bs addHover = Interact(addSubtitle);
                if (addHover.clicked) {
                    gSubtitleTranscribePopupOpen = true;
                    gSubtitlePopupOpenedAt = GetTime();
                    if (gSubtitleTranscriptionRequestId == 0) {
                        gSubtitleTranscriptionState = 0;
                        gSubtitleTranscriptionProgress = 0.f;
                        gSubtitleTranscriptionDetail[0] = 0;
                        (void)PlutoWhisperCaptureSelectedSource();
                    }
                    PlutoWhisperModelsRefresh();
                }
                CLAY(addSubtitle, {.layout = {.sizing = {FIX(42), FIX(42)}, .childAlignment = AL_C},
                                   .backgroundColor = BtnBg(C_RCARDB, RGB(54, 56, 63), addHover),
                                   .cornerRadius = CLAY_CORNER_RADIUS(13),
                                   .border = {.color = Mix(C_RCARDB, C_TEXT, addHover.hover * .22f),
                                              .width = CLAY_BORDER_OUTSIDE(1)}}) {
                    Icon(I_PLUS, 19, C_TEXT);
                }
            }
            if (subtitleCount > 0) {
                CLAY(CLAY_ID("SubtitleCueList"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {8, 8, 8, 8}, .childGap = 4},
                                                  .backgroundColor = C_RCARD,
                                                  .cornerRadius = {0.f, 0.f, 16.f, 16.f}}) {
                    for (int i = 0; i < gTimelineClipCount; i++) {
                        TimelineClipUi *cue = &gTimelineClips[i];
                        if (!cue->isSubtitle) continue;
                        Clay_ElementId cueId = CLAY_IDI("SubtitleCueRow", i);
                        Bs cueHover = Interact(cueId);
                        if (cueHover.clicked) {
                            float cueStart = cue->start;
                            PlutoTimelineSelect(i);
                            gPlayhead = cueStart;
                            gTimelinePlaying = false;
                            gPreviewDirty = true;
                        }
                        CLAY(cueId, {.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = {GROW, FIX(34)}, .padding = {9, 7, 7, 7}, .childAlignment = AL_Y, .childGap = 8},
                                      .backgroundColor = BtnBg(RGB(31, 32, 37), RGB(43, 45, 52), cueHover),
                                      .cornerRadius = CLAY_CORNER_RADIUS(8),
                                      .border = {.color = Mix(C_RCARDB, RGB(74, 77, 85), cueHover.hover * .4f),
                                                 .width = CLAY_BORDER_OUTSIDE(1)}}) {
                            CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}}}) {
                                Tx(Fmt("%.31s", cue->subtitleText[0] ? cue->subtitleText : "(empty subtitle)"), 12.5f, C_TEXT, 0);
                            }
                            Tx(Fmt("%s–%s", Timecode(cue->start), Timecode(cue->start + cue->duration)), 10.5f, C_MUTED, 0);
                        }
                    }
                }
            }
        }
    }
    GAPY(7)
    /* Content: keep only the actual editable field surfaced, not another card. */
    CLAY(CLAY_ID("TextContentCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 8}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(24)}, .childAlignment = AL_Y}}) {
            Tx("Content", 18, C_TEXT, 1);
            HSPACE
            if (gTextTitleSelected) Tx("Editing title", 13, C_MUTED, 0);
        }
        Clay_ElementId field = CLAY_ID("TextContentField");
        bool over = Clay_PointerOver(field);
        Bs fb = Interact(field);
        if (fb.clicked) {
            gTextInputFocused = true;
            gTextFocusStartedAt = GetTime();
            gTextCaretT = gTextFocusStartedAt;
            gTextColorEditorOpen = false;
            gTextColorEditIndex = -1;
            gTextColorHexFocused = false;
        }
        Vector2 pointer = GetMousePosition();
        bool overPreviewText = gTextTitleSelected && gTextBoxScreenRectValid &&
                               CheckCollisionPointRec(pointer, gTextBoxScreenRect);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !over && !overPreviewText && gTextInputFocused &&
            GetTime() - gTextFocusStartedAt > 0.08) {
            gTextInputFocused = false;
            gTextFocusStartedAt = 0.0;
        }
        if (gTextInputFocused) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                gTextInputFocused = false;
                gTextFocusStartedAt = 0.0;
            }
            if (IsKeyPressed(KEY_BACKSPACE)) PlutoTextBackspace();
            if (IsKeyPressed(KEY_ENTER)) PlutoTextAppendCodepoint('\n');
            int cp;
            while ((cp = GetCharPressed()) != 0) PlutoTextAppendCodepoint(cp);
        }
        char contentPreview[STELLA_TEXT_MAX + 2];
        bool showCaret = gTextInputFocused && fmod(GetTime() - gTextCaretT, 1.08) <= .58;
        if (gTextContent[0]) {
            snprintf(contentPreview, sizeof(contentPreview), "%s%s", gTextContent, showCaret ? "|" : "");
        } else {
            snprintf(contentPreview, sizeof(contentPreview), "%s", gTextInputFocused && showCaret ? "|" : "Click here and type your title");
        }
        CLAY(field, {.layout = {COLM, .sizing = {GROW, FIX(92)}, .padding = {11, 11, 9, 9}},
                     .backgroundColor = Mix(C_RFIELD, RGB(46, 47, 54), gTextInputFocused ? .45f : fb.hover * .3f),
                     .cornerRadius = CLAY_CORNER_RADIUS(10),
                     .border = {.color = gTextInputFocused ? RGB(104, 108, 120) : Mix(C_RFIELD, RGB(76, 79, 88), fb.hover),
                                .width = CLAY_BORDER_OUTSIDE(1)}}) {
            TxWrap(contentPreview, 16,
                   gTextContent[0] || gTextInputFocused ? C_TEXT : C_MUTED, 0);
        }
        if (!gTextTitleSelected)
            TxWrap("Choose the Text tool, then drag on the preview to create a text box.", 12.5f, C_MUTED, 0);
    }

    GAPY(6)
    /* Position: section title plus controls, without an outer card border. */
    CLAY(CLAY_ID("TextPosCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 8}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(24)}, .childAlignment = AL_Y}}) {
            Tx("Position", 18, C_TEXT, 1);
            HSPACE
            if (IconBtn(CLAY_ID("TxtPosReset"), I_RESET, 24, 13, RGB(104, 105, 112), false, 6)) {
                gTextPosX = .5f; gTextPosY = .78f;
            }
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 12, .childAlignment = AL_Y}}) {
            TextPosGrid();
            CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 8}}) {
                TextPosField(0, "X", &gTextPosX, .03f, .97f);
                TextPosField(1, "Y", &gTextPosY, .04f, .96f);
            }
        }
    }

    GAPY(6)
    /* Typography: size and alignment share one simple, unboxed section. */
    CLAY(CLAY_ID("TextFontCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 8}}) {
        Tx("Typography", 18, C_TEXT, 1);
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(27)}, .childAlignment = AL_Y}}) {
            Tx("Font size", 15, C_DIM, 0);
            HSPACE
            float sizeValue = (float)gTextFontSize;
            if (ValBoxEditable(CLAY_ID("TextFontSize"), &sizeValue, "px", 82, 27, 16, 13, 8, 8.f, 180.f, 0)) {
                gTextFontSize = (int)roundf(sizeValue);
                gTextAutoFit = false;
            }
        }
        {
            static bool sizeDrag;
            Clay_ElementId sid = CLAY_ID("TextSizeSlider");
            Bs sb = Interact(sid);
            float t = ((float)gTextFontSize - 8.f) / 172.f;
            float before = t;
            FxSliderLogic(sid, &t, &sizeDrag, 7.f);
            if (fabsf(t - before) > .0001f) {
                gTextFontSize = (int)roundf(8.f + t * 172.f);
                gTextAutoFit = false;
            }
            CLAY(sid, {.layout = {.sizing = {GROW, FIX(16)}},
                       .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(150, 152, 160), C_CARD,
                                                   ((float)gTextFontSize - 8.f) / 172.f,
                                                   (sb.hover + (sizeDrag ? 1.f : 0.f)) * 1.5f, 0)}}) {}
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(34)}, .childAlignment = AL_Y}}) {
            Tx("Font", 15, C_DIM, 0);
            HSPACE
            Clay_ElementId fontButton = CLAY_ID("TextFontPicker");
            Bs fontHover = Interact(fontButton);
            if (fontHover.clicked) gTextFontMenuOpen = !gTextFontMenuOpen;
            CLAY(fontButton, {.layout = {.sizing = {FIX(145), FIX(32)}, .padding = {9, 8, 0, 0}, .childAlignment = AL_Y},
                              .backgroundColor = BtnBg(C_RFIELD, RGB(46, 47, 54), fontHover),
                              .cornerRadius = CLAY_CORNER_RADIUS(9),
                              .border = {.color = Mix(C_RCARDB, RGB(91, 95, 105), fontHover.hover * .55f),
                                         .width = CLAY_BORDER_OUTSIDE(1)}}) {
                Tx(Fmt("%.18s", gTextFontName[0] ? gTextFontName : "System"), 13, C_TEXT, 0);
                HSPACE
                Icon(I_CHEV_DN, 11, C_MUTED);
            }
            Clay_BoundingBox picker = Clay_GetElementData(fontButton).boundingBox;
            gTextFontPickerRect = (Rectangle){picker.x, picker.y, picker.width, picker.height};
            const float menuW = 224.f, menuH = fminf(246.f, 8.f * 29.f + 8.f), edge = 8.f;
            float menuX = picker.x;
            float menuY = picker.y + picker.height + 5.f;
            if (menuX + menuW > GetScreenWidth() - edge) menuX = GetScreenWidth() - menuW - edge;
            if (menuX < edge) menuX = edge;
            if (menuY + menuH > GetScreenHeight() - edge) menuY = picker.y - menuH - 5.f;
            if (menuY < edge) menuY = edge;
            gTextFontMenuRect = (Rectangle){menuX, menuY, menuW, menuH};
        }
        if (gTextFontMenuOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 menuMouse = GetMousePosition();
            if (CheckCollisionPointRec(menuMouse, gTextFontMenuRect)) {
                int option = (int)floorf((menuMouse.y - gTextFontMenuRect.y - 4.f + gTextFontMenuScroll) / 29.f);
                if (option == 0) {
                    gTextFontFamily = 0; gTextFontPath[0] = 0;
                    snprintf(gTextFontName, sizeof(gTextFontName), "System");
                } else if (option > 0 && option <= gSystemFontCount) {
                    gTextFontFamily = 0;
                    snprintf(gTextFontPath, sizeof(gTextFontPath), "%s", gSystemFonts[option - 1].path);
                    snprintf(gTextFontName, sizeof(gTextFontName), "%s", gSystemFonts[option - 1].name);
                }
                gTextFontMenuScroll = 0.f;
                gTextFontMenuOpen = false;
            } else if (!CheckCollisionPointRec(menuMouse, gTextFontPickerRect)) gTextFontMenuOpen = false;
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 6}}) {
            if (IconBtn(CLAY_ID("TextBold"), I_BOLD, 31, 18, C_DIM, gTextBold, 7)) gTextBold = !gTextBold;
            if (IconBtn(CLAY_ID("TextItalic"), I_ITALIC, 31, 18, C_DIM, gTextItalic, 7)) gTextItalic = !gTextItalic;
            if (IconBtn(CLAY_ID("TextUnderline"), I_UNDERLINE, 31, 18, C_DIM, gTextUnderline, 7)) gTextUnderline = !gTextUnderline;
            GAPX(8)
            if (IconBtn(CLAY_ID("TextAlignLeft"), I_ALIGN_LEFT, 31, 18, C_DIM, gTextAlign == 0, 7)) gTextAlign = 0;
            if (IconBtn(CLAY_ID("TextAlignCenter"), I_ALIGN_CENTER, 31, 18, C_DIM, gTextAlign == 1, 7)) gTextAlign = 1;
            if (IconBtn(CLAY_ID("TextAlignRight"), I_ALIGN_RIGHT, 31, 18, C_DIM, gTextAlign == 2, 7)) gTextAlign = 2;
        }
    }

    GAPY(6)
    /* A single progress value animates the one active colour editor. It avoids
       four competing row animations and keeps the inspector height stable. */
    Clay_ElementId textColorAnimId = CLAY_ID("TextColorEditorAnim");
    /* AnimLin normally initializes to its target. Prime this one shared slot
       at zero so the first-ever orb open also gets the full smooth expansion. */
    (void)AnimPtr(textColorAnimId.id ^ 0x5BD1E995u, 0.f);
    float textColorEditorT = EaseIO(AnimLin(textColorAnimId.id,
                                            gTextColorEditorOpen ? 1.f : 0.f, .18f));
    if (!gTextColorEditorOpen && textColorEditorT <= .004f)
        gTextColorEditIndex = -1;
    /* Appearance: color swatches open a compact hex editor; style rows stay flat. */
    CLAY(CLAY_ID("TextLookCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 7}}) {
        Tx("Appearance", 18, C_TEXT, 1);
        Tx("Colors", 13, C_MUTED, 0);
        TextColorRow(0, "Text color", textColorEditorT);
        TextColorRow(1, "Outline color", textColorEditorT);
        TextColorRow(2, "Shadow color", textColorEditorT);
        TextColorRow(3, "Background color", textColorEditorT);
    }
    PlutoTextAutoUpdateObserve();
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
    if (gTextFontMenuOpen && CheckCollisionPointRec(m, gTextFontMenuRect) && fabsf(wh) > .001f) {
        float rows = (float)(gSystemFontCount + 1) * 29.f;
        gTextFontMenuScroll = Clampf(gTextFontMenuScroll - wh * 34.f, 0.f,
                                     fmaxf(0.f, rows - (gTextFontMenuRect.height - 8.f)));
        wh = 0.f;
    }
    if (wh != 0.f && m.x >= bb.x && m.x <= bb.x + bb.width && m.y >= bb.y && m.y <= bb.y + bb.height) {
        gScrollT -= wh * 60.f;
        gFollow = false;
    }
    if (gFollow) gScrollT = maxS;
    /* draggable scrollbar: wide invisible hit area around the thin thumb; click the track to jump */
    static bool sbDrag = false; static float sbGrab = 0.f;
    float sbTop = bb.y + 10.f, sbTrack = bb.height - 20.f;
    float sbThumb = (maxS > 1.f && cb.height > 1.f) ? fmaxf(36.f, sbTrack * bb.height / cb.height) : sbTrack;
    if (maxS > 1.f && bb.width > 0.f) {
        Rectangle hit = {bb.x + bb.width - 20.f, bb.y, 20.f, bb.height};
        float thumbY = sbTop + (sbTrack - sbThumb) * (gScroll / maxS);
        if (CheckCollisionPointRec(m, hit)) gHand = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, hit)) {
            sbDrag = true;
            sbGrab = (m.y >= thumbY && m.y <= thumbY + sbThumb) ? m.y - thumbY : sbThumb * .5f;
        }
        if (sbDrag) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float f = (m.y - sbGrab - sbTop) / fmaxf(1.f, sbTrack - sbThumb);
                f = f < 0.f ? 0.f : f > 1.f ? 1.f : f;
                gScrollT = gScroll = f * maxS; gFollow = false;
            } else sbDrag = false;
        }
    } else sbDrag = false;
    if (gScrollT > maxS) gScrollT = maxS;
    if (gScrollT < 0.f) gScrollT = 0.f;
    if (!sbDrag) gScroll += (gScrollT - gScroll) * (1.f - expf(-20.f * gDt));
    if (fabsf(gScrollT - gScroll) < 0.3f) gScroll = gScrollT;
    if (gScroll > maxS) gScroll = maxS;
    gSB = (maxS > 1.f && bb.width > 0.f);
    if (gSB) {
        float frac = gScroll / maxS;
        gSBR = (Rectangle){bb.x + bb.width - 9.f, sbTop + (sbTrack - sbThumb) * frac, sbDrag ? 5.f : 4.f, sbThumb};
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
                        if (gRightTab != i) {
                            PlutoTextAutoUpdateFlush();
                            PlutoTimelineEndPropertyEdit();
                            gTextInputFocused = false;
                            gTextFocusStartedAt = 0.0;
                            TextColorEditorReset();
                            gTabDir = (i > gRightTab) ? 1 : -1;
                            gRightTab = i; gScroll = gScrollT = 0.f;
                            gFollow = false; gTabT = 0.f; gTabE = 0.f;
                        }
                    }
            }
        }
        CLAY(CLAY_ID("InspBody"), {.layout = {.sizing = {GROW, fitBody ? FIT : GROW}}, RPANEL,
                                   .clip = {.horizontal = true, .vertical = true, .childOffset = {gTabDir * (1.f - gTabE) * TAB_SLIDE_PX, -gScroll}}}) {
            CLAY(CLAY_ID("InspContent"), {.layout = {COLM, .sizing = {GROW, fitBody ? FIT : CLAY_SIZING_FIT(gInspViewH)}, .padding = {13, 13, 15, 13}, .childGap = 10},
                                       .clip = {.horizontal = true}}) {
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

