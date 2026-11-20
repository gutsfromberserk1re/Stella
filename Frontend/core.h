/* Pluto frontend core: shared state, palette, animation and UI helpers. */
/* ------------------------------------------------------------------ */
/* palette                                                             */
/* ------------------------------------------------------------------ */
#define RGB(r,g,b)     ((Clay_Color){(r), (g), (b), 255})
#define RGBA(r,g,b,a)  ((Clay_Color){(r), (g), (b), (a)})
#define C_BG        RGB(11,11,13)
#define C_PANEL     RGB(23,24,28)
#define C_PBORDER   RGB(36,38,43)
#define C_CARD      RGB(30,31,36)
#define C_CARDB     RGB(42,44,50)
#define C_FIELD     RGB(38,39,45)
#define C_SEG       RGB(16,17,20)
#define C_SEL       RGB(46,48,55)
#define C_TEXT      RGB(237,237,240)
#define C_DIM       RGB(190,194,202)
#define C_MUTED     RGB(138,142,152)
#define C_ACCENT    RGB(64,120,255)
#define C_ROW       RGB(19,20,24)
/* right-panel palette (sampled from the reference) */
#define C_ICON      RGB(140,142,150)   /* right-panel icons + secondary labels */
#define C_PGRAD     RGB(27,28,31)      /* sentinel: drawn as a vertical gradient 28,29,32 -> 21,22,24 */
#define C_RCARD     RGB(24,25,28)
#define C_RCARDB    RGB(38,39,41)
#define C_RFIELD    RGB(33,34,38)
#define C_VALBOX    RGB(33,34,38)
#define RPANEL_R(r) .backgroundColor = C_PGRAD, .cornerRadius = CLAY_CORNER_RADIUS(r), \
                    .border = {.color = RGB(40,41,43), .width = CLAY_BORDER_OUTSIDE(1)}
#define RPANEL      RPANEL_R(22)
#define RCARD       .backgroundColor = C_RCARD, .cornerRadius = CLAY_CORNER_RADIUS(18), \
                    .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)}

/* ------------------------------------------------------------------ */
/* small layout shorthands                                             */
/* ------------------------------------------------------------------ */
#define GROW        CLAY_SIZING_GROW(0)
#define FIT         CLAY_SIZING_FIT(0)
#define FIX(n)      CLAY_SIZING_FIXED(((n) >= 8 ? (n) + 2.25f : (n)))
#define COLM        .layoutDirection = CLAY_TOP_TO_BOTTOM
#define AL_C        {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
#define AL_Y        {.y = CLAY_ALIGN_Y_CENTER}
#define HSPACE      CLAY_AUTO_ID({.layout = {.sizing = {.width = GROW}}}) {}
#define VSPACE      CLAY_AUTO_ID({.layout = {.sizing = {.height = GROW}}}) {}
#define GAPX(n)     CLAY_AUTO_ID({.layout = {.sizing = {.width = FIX(n), .height = FIX(1)}}}) {}
#define GAPY(n)     CLAY_AUTO_ID({.layout = {.sizing = {.width = FIX(1), .height = FIX(n)}}}) {}
#define PANEL       .backgroundColor = C_PANEL, .cornerRadius = CLAY_CORNER_RADIUS(16), \
                    .border = {.color = C_PBORDER, .width = CLAY_BORDER_OUTSIDE(1)}
#define CARD        .backgroundColor = C_CARD, .cornerRadius = CLAY_CORNER_RADIUS(14), \
                    .border = {.color = C_CARDB, .width = CLAY_BORDER_OUTSIDE(1)}

/* ------------------------------------------------------------------ */
/* custom-drawn element kinds                                          */
/* ------------------------------------------------------------------ */
enum {
    I_NONE, I_CROP, I_POS, I_SCALE, I_RESET, I_LINK, I_ROT, I_PLUS, I_CHEV_R, I_CHEV_L,
    I_CHEV_UP, I_CHEV_DN, I_SEARCH, I_LIBRARY, I_UNDO, I_REDO, I_EXPORT, I_NOTE, I_PALETTE,
    I_GRADING, I_SLIDERS, I_SUN, I_DROP, I_MOUNTAIN, I_DISTORT, I_SKIP_B, I_SKIP_F, I_PLAY,
    I_PAUSE, I_LOOP, I_GRID, I_POINTER, I_HAND, I_SCISSORS, I_TEXT, I_TRASH, I_KF_PREV, I_KF_NEXT,
    I_KF_DIAMOND, I_ZOOM_OUT, I_ZOOM_IN, I_MAGNET, I_FILM, I_BOWTIE, I_SPARKLE,
    I_MINIMIZE, I_MAXIMIZE, I_CLOSE,
    K_APP_LOGO = 99,
    K_THUMB = 100, K_KNOB, K_WHEEL, K_SWITCH, K_SLIDER, K_ROTRULER, K_RULER, K_PREVIEW,
    K_CLIP, K_SCROLLBAR, K_PILL, K_FILTER, K_CHIP, K_GLOW, K_SPLIT, K_ROT_ICON
};

typedef struct { int kind; Clay_Color a, b, c; float v, f; int i; } Cust;
static Cust gCust[4096];
static int  gCustN;
static Cust *NC(int k, Clay_Color a, Clay_Color b, Clay_Color c, float v, float f, int i) {
    Cust *p = &gCust[gCustN++ & 4095];
    p->kind = k; p->a = a; p->b = b; p->c = c; p->v = v; p->f = f; p->i = i;
    return p;
}

/* ------------------------------------------------------------------ */
/* globals / state                                                     */
/* ------------------------------------------------------------------ */
static float gFK[3] = {1.f, 1.f, 1.f};   /* per-weight font metrics normalisation */
static float gDpiScale = 1.f;
static float gDt;
static float gWheelDelta;
static bool gCtrlDown;
static bool  gHand, gCross;
static int   gLeftTab = 0, gRightTab = 0;
static int   gPreviewRes = 720;
static bool  gPreviewMenuOpen;
static float gPreviewMenuT;
static bool  gPreviewDirty;
static int gPreviewFps = 30;
static bool gPreviewFpsMenuOpen;
static float gPreviewFpsMenuT;
static bool gPreviewNavigating;
static float gPreviewDisplayedAt = -1.f;
static bool  gTimelineScrubbing;
static float gPreviewDebounce;
static uint64_t gPreviewTaskId;
static float gPreviewRequestAt;
static int gPreviewRequestRes;
static double gPreviewRequestStartedAt;
static double gPreviewTelemetryAt;
static Texture2D gPreviewFrame;
static uint8_t *gPreviewPixels;
static size_t gPreviewPixelCapacity;
static Rectangle gPreviewQualityRect;
static Rectangle gPreviewFpsRect;
static float gMediaZoom = 1.f;
static char gMediaHoverName[260];
static Rectangle gMediaHoverRect;
static Rectangle gMediaDropPreview;
static bool gMediaDropPreviewVisible;
static int   gTimelineTool;
static float gTimelineZoom = 1.f, gTimelinePan, gPlayhead;
static bool  gTimelineSnap = true, gTimelinePlaying, gTimelineGrid;
static int gTransitionFocusClip = -1;
static char gTransitionType[16] = "none";
static float gTransitionDuration = 1.f;
typedef struct {
    Rectangle rect;
    Rectangle durationRect;
    int clip;
    bool applied;
} TransitionButton;
static TransitionButton gTransitionButtons[64];
static int gTransitionButtonCount;
static int   gTimelineSelected = -1, gMediaDragIndex = -1, gClipDragIndex = -1;
static int   gClipDragTrack;
static float gClipDragOffset, gClipDragOrigStart, gTimelinePanMouse, gTimelinePanOrig;
static int   gClipTrimIndex = -1, gClipTrimEdge;
static float gClipTrimOrigStart, gClipTrimOrigEnd, gClipTrimOrigOffset, gClipTrimSourceDuration;
static bool  gTimelinePanning;
static uint64_t gClipSerial = 1;
static int   gWindowAction; /* 0 none, 1 minimize, 2 maximize/restore, 3 close */
static bool  gWantQuit;
#ifdef _WIN32
static WNDPROC gPreviousWndProc;
#endif

/* right panel state */
static int   gOpenAcc = 0;                            /* single-open accordion, -1 none */
static bool  gLink[4] = {false, true, false, false};  /* position, scale, crop, distort */
static int   gOpenTool = -1;                          /* -1 none, 0 crop tools, 1 distort */
static float gScroll, gScrollT;                       /* inspector scroll (pixels down)   */
static bool  gFollow;                                 /* keep pinned to bottom while expanding */
static float gInspViewH;
static float gTabT = 1.f;                             /* tab-switch transition 0..1 */
static float gTabE = 1.f;                             /* eased */
static Rectangle gFadeR; static float gFadeA;         /* fade overlay while switching */
static bool  gSB; static Rectangle gSBR;              /* inspector scrollbar thumb */

/* left panel state */
static float gAudS, gAudT;                            /* audio panel scroll */
static bool  gSwOn[2];                                /* fade in / fade out */
static float gKnob[4] = {.5f, .5f, .5f, .5f};        /* volume, pan, bass, treble (0..1, .5 = default) */
static float gKnobStartV, gKnobStartY;
static float gLTabT = 1.f, gLTabE = 1.f;             /* left tab-switch transition */
static Rectangle gLFadeR; static float gLFadeA;      /* fade overlay while switching Media/Audio */

/* resizable panels (defaults already include the FIX() +2.25 so the look is unchanged) */
#define LEFT_MIN   250.f
#define LEFT_MAX   440.f
#define RIGHT_MIN  376.25f   /* inspector cards are laid out for this width, so it can only grow */
#define RIGHT_MAX  520.f
#define TL_MIN     200.f
#define TL_MAX     560.f
#define CENTER_MIN 400.f     /* preview never gets narrower than this */
#define MAIN_MIN   300.f     /* panels row never gets shorter than this */
static float gLeftW = 288.25f, gRightW = 376.25f, gTimeH = 320.25f;
static int   gSplit;                                  /* 0 none, 1 left, 2 right, 3 timeline */
static float gSplitM, gSplitV;                        /* mouse + size when the drag started */
static int   gResize;                                 /* cursor: 0 normal, 1 horizontal, 2 vertical */

/* values */
static float gNum[4] = {0, 0, 100, 100};              /* X, Y, W, H */
static float gMini[8];                                /* crop/distort numeric fields */
static float gRot, gRotT; static bool gRotDrag;       /* degrees -180..180 */
#define NSL 48
static float gSl[NSL], gSlT[NSL]; static bool gSlDrag[NSL]; /* slider 0..1 (0 = opacity, 40/41 = fades) */
static Vector2 gWh[3], gWhT[3]; static bool gWhDrag[3];     /* wheel puck, unit disk */
static int   gHsl = 0;                                /* selected HSL chip */
static bool  gTransOpen = true;                       /* effects: transitions card */
static int   gFilter = 0;                             /* effects: selected preset */
static uint32_t gDragId;                              /* active drag target (slider/ruler) */
static int   gDragWheel = -1;
static uint32_t gEditValueId, gLastValueClickId, gValueDragId;
static char gEditValueText[32];
static int gEditValueLen;
static double gLastValueClickTime;
static bool gValueDragging;
static float gValueDragX, gValueDragStart;
static bool gScrubCursor;

#define MAX_IMPORTED_MEDIA 64
typedef struct {
    char path[1024], name[260], kind[16];
    char waveformPath[1024], thumbnailPath[1024];
    Texture2D waveform, thumbnail;
    float duration;
    bool hasAudio;
} ImportedMedia;
static ImportedMedia gImported[MAX_IMPORTED_MEDIA];
static int gImportedCount;
static int gMediaSelected = -1;
static char gBackendMessage[256];

#define MAX_TIMELINE_CLIPS 256
typedef struct {
    char id[64], label[260], path[1024], groupId[64], linkedId[64], kind[16];
    char transitionType[16];
    int track, mediaIndex; /* 0..3 = video 1..4, 4..5 = audio 1..2 */
    float start, duration, sourceOffset, sourceDuration, transitionDuration;
    bool hasAudio;
    float keyframes[32];
    int keyframeCount;
    float keyframeValues[32][6]; /* x, y, scaleW, scaleH, rotation, opacity per keyframe */
} TimelineClipUi;

/* Interpolated transform values at a clip-relative time. Mirrors the Rust
   piecewise-linear interpolation shared by preview and export. */
static void PlutoKeyframeTransform(const TimelineClipUi *clip, float local, float out[6]) {
    if (!clip) return;
    for (int i = 0; i < 6; i++) out[i] = 0.f;
    if (clip->keyframeCount <= 0 || clip->keyframeCount > 32) {
        out[0] = 0.f; out[1] = 0.f; out[2] = 100.f; out[3] = 100.f; out[4] = 0.f; out[5] = 100.f;
        return;
    }
    float times[32];
    float values[32][6];
    for (int i = 0; i < clip->keyframeCount; i++) {
        times[i] = clip->keyframes[i];
        for (int v = 0; v < 6; v++) values[i][v] = clip->keyframeValues[i][v];
    }
    float duration = clip->duration > 0.f ? clip->duration : 0.f;
    float t = local < 0.f ? 0.f : (local > duration ? duration : local);
    if (t <= times[0]) { for (int v = 0; v < 6; v++) out[v] = values[0][v]; return; }
    float prev = times[0];
    for (int i = 1; i < clip->keyframeCount; i++) {
        if (t <= times[i]) {
            float span = times[i] - prev;
            float f = span > 0.0001f ? (t - prev) / span : 1.f;
            for (int v = 0; v < 6; v++) out[v] = values[i - 1][v] + (values[i][v] - values[i - 1][v]) * f;
            return;
        }
        prev = times[i];
    }
    for (int v = 0; v < 6; v++) out[v] = values[clip->keyframeCount - 1][v];
}

static TimelineClipUi gTimelineClips[MAX_TIMELINE_CLIPS];
static int gTimelineClipCount;
static float gClipDragSnapshotStart[MAX_TIMELINE_CLIPS];
static int gClipDragSnapshotTrack[MAX_TIMELINE_CLIPS];
static bool gTimelineMultiSelect[MAX_TIMELINE_CLIPS];
static int gTimelineSelectionAnchor = -1;
static bool gTimelineContextOpen;
static bool gTimelineContextConsumedClick;
static int gTimelineContextClip = -1;
static Vector2 gTimelineContextPos;
static Rectangle gTimelineContextRect;
static int gTimelineVideoTracks = 1, gTimelineAudioTracks = 1;
static float TimelinePps(void);
static float TimelineDuration(void);

/* rotation ruler geometry (inside the 322px wide ruler box) */
#define RULER_L 22.f
#define RULER_R 22.5f

/* playhead is drawn after everything else so rows never cover it */
static bool gPH; static Vector2 gPHPos; static float gPHLen;

/* formatted text lives in a ring so Clay can read it until render */
static char gFb[160][40]; static int gFbN;
static const char *Fmt(const char *f, ...) {
    char *b = gFb[gFbN++ % 160];
    va_list ap; va_start(ap, f); vsnprintf(b, 40, f, ap); va_end(ap);
    return b;
}

/* ------------------------------------------------------------------ */
/* crisp font cache: one atlas per (font, exact pixel size)            */
/* ------------------------------------------------------------------ */
static char gFontPath[3][260];
static bool gFontOK[3];
static int  gFcps[100], gFcpN;
typedef struct { int slot, px; Font f; } FontEnt;
static FontEnt gFC[160];
static int gFCN;

static int PxOf(int slot, float size) {
    int p = (int)roundf(size * gFK[slot] * gDpiScale);
    return p < 6 ? 6 : p > 192 ? 192 : p;
}
static Font FontPx(int slot, int px) {
    if (slot < 0 || slot > 2) slot = 0;
    if (!gFontOK[slot]) return GetFontDefault();
    for (int i = 0; i < gFCN; i++)
        if (gFC[i].slot == slot && gFC[i].px == px) return gFC[i].f;
    if (gFCN >= 160) return GetFontDefault();
    Font f = LoadFontEx(gFontPath[slot], px, gFcps, gFcpN);
    if (f.texture.id == 0) { gFontOK[slot] = false; return GetFontDefault(); }
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    gFC[gFCN].slot = slot; gFC[gFCN].px = px; gFC[gFCN].f = f;
    return gFC[gFCN++].f;
}

/* ------------------------------------------------------------------ */
/* animation table                                                     */
/* ------------------------------------------------------------------ */
typedef struct { uint32_t id; float v; bool used; } AnimSlot;
static AnimSlot gAnim[2048];

static float Anim(uint32_t key, float target, float speed) {
    for (int i = 0; i < 2048; i++) {
        if (gAnim[i].used && gAnim[i].id == key) {
            float d = target - gAnim[i].v;
            gAnim[i].v += d * (1.0f - expf(-speed * gDt));
            if (fabsf(target - gAnim[i].v) < 0.0008f) gAnim[i].v = target;
            return gAnim[i].v;
        }
    }
    for (int i = 0; i < 2048; i++)
        if (!gAnim[i].used) { gAnim[i].used = true; gAnim[i].id = key; gAnim[i].v = target; return target; }
    return target;
}

/* fixed-duration linear progress 0..1 (always finishes exactly, no slow tail) */
static float AnimLin(uint32_t key, float target, float dur) {
    key ^= 0x5BD1E995u;
    for (int i = 0; i < 2048; i++) {
        if (gAnim[i].used && gAnim[i].id == key) {
            float step = gDt / dur, d = target - gAnim[i].v;
            if (fabsf(d) <= step) gAnim[i].v = target;
            else gAnim[i].v += (d > 0 ? step : -step);
            return gAnim[i].v;
        }
    }
    for (int i = 0; i < 2048; i++)
        if (!gAnim[i].used) { gAnim[i].used = true; gAnim[i].id = key; gAnim[i].v = target; return target; }
    return target;
}
static float EaseIO(float t) { if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3.f - 2.f * t); }

static float *AnimPtr(uint32_t key, float init) {
    for (int i = 0; i < 2048; i++)
        if (gAnim[i].used && gAnim[i].id == key) return &gAnim[i].v;
    for (int i = 0; i < 2048; i++)
        if (!gAnim[i].used) { gAnim[i].used = true; gAnim[i].id = key; gAnim[i].v = init; return &gAnim[i].v; }
    static float dummy; dummy = init; return &dummy;
}

/* damped spring: k = stiffness, d = damping (lower d = bouncier) */
static float Spring(uint32_t key, float target, float k, float d) {
    float *p = AnimPtr(key ^ 0x7F4A7C15u, target);
    float *v = AnimPtr(key ^ 0x2545F491u, 0.f);
    int n = (int)ceilf(gDt / 0.004f); if (n < 1) n = 1;
    float h = gDt / n;
    for (int i = 0; i < n; i++) { *v += (k * (target - *p) - d * *v) * h; *p += *v * h; }
    return *p;
}

static Clay_Color Mix(Clay_Color a, Clay_Color b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return (Clay_Color){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
static Clay_Color Dark(Clay_Color c, float t) { return Mix(c, (Clay_Color){0, 0, 0, c.a}, t); }

typedef struct { float hover, press; bool clicked; } Bs;
static Bs Interact(Clay_ElementId id) {
    bool over = Clay_PointerOver(id);
    if (over) gHand = true;
    Bs b;
    b.hover   = Anim(id.id, over ? 1.f : 0.f, 18.f);
    /* Keep controls calm while clicking; hover remains the interaction cue. */
    b.press   = 0.f;
    b.clicked = over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    return b;
}
static Clay_Color BtnBg(Clay_Color base, Clay_Color hov, Bs b) { return Mix(base, hov, b.hover * 0.72f); }

/* ------------------------------------------------------------------ */
/* text + icon helpers                                                 */
/* ------------------------------------------------------------------ */
static Clay_String S(const char *s) { return (Clay_String){.isStaticallyAllocated = false, .length = (int32_t)strlen(s), .chars = s}; }

static void Tx(const char *s, float size, Clay_Color c, int font) {
    CLAY_TEXT(S(s), CLAY_TEXT_CONFIG({.fontId = font ? 2 : 0, .fontSize = size + 2.25f, .textColor = c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
}
static void TxWrap(const char *s, float size, Clay_Color c, int font) {
    CLAY_TEXT(S(s), CLAY_TEXT_CONFIG({.fontId = font ? 2 : 0, .fontSize = size + 2.25f, .textColor = c, .wrapMode = CLAY_TEXT_WRAP_WORDS}));
}
static void TxMedium(const char *s, float size, Clay_Color c) {
    CLAY_TEXT(S(s), CLAY_TEXT_CONFIG({.fontId = 1, .fontSize = size + 2.25f, .textColor = c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
}
static void IconV(int k, float sz, Clay_Color c, float v) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = FIX(sz), .height = FIX(sz)}},
                  .custom = {.customData = NC(k, c, c, c, v, 0, 0)}}) {}
}
static void Icon(int k, float sz, Clay_Color c) { IconV(k, sz, c, (k == I_RESET || k == I_ROT) ? 0.f : 1.f); }

static float ResetSpin(Clay_ElementId id, bool clicked) {
    uint32_t key = id.id ^ 0xA511E9B3u;
    float *progress = AnimPtr(key ^ 0x5BD1E995u, 0.f);
    if (clicked) *progress = 1.f;
    if (*progress <= 0.f) return 0.f;
    float angle = -360.f * EaseIO(1.f - *progress);
    *progress = fmaxf(0.f, *progress - gDt / .30f);
    return angle;
}

static void ResetIcon(Clay_ElementId id, int icon, float sz, Clay_Color c, bool clicked) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = FIX(sz), .height = FIX(sz)}},
                  .custom = {.customData = NC(K_ROT_ICON, c, c, c, ResetSpin(id, clicked), 0, icon)}}) {}
}

static bool IconBtn(Clay_ElementId id, int icon, float box, float isz, Clay_Color base, bool sel, float rad) {
    Bs b = Interact(id);
    Clay_Color ic = Mix(Mix(base, C_TEXT, b.hover), C_TEXT, b.press);
    Clay_Color bg = sel ? Mix(C_SEL, RGB(90, 95, 106), b.press * 0.6f) : RGBA(255, 255, 255, 14 * b.hover + 56 * b.press);
    CLAY(id, {.layout = {.sizing = {FIX(box), FIX(box)}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(rad)}) {
        if (icon == I_RESET || icon == I_ROT) ResetIcon(id, icon, isz, ic, b.clicked);
        else Icon(icon, isz, ic);
    }
    return b.clicked;
}

static void WindowButton(const char *key, int icon) {
    Clay_ElementId id = CLAY_SIDI(S(key), 0);
    Bs b = Interact(id);
    Clay_Color bg = icon == I_CLOSE
        ? Mix(RGBA(0, 0, 0, 0), RGB(190, 48, 56), b.hover)
        : RGBA(255, 255, 255, 18.f * b.hover);
    CLAY(id, {.layout = {.sizing = {FIX(36), FIX(32)}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
        Icon(icon, 13, C_TEXT);
    }
    if (b.clicked) {
        gWindowAction = icon == I_MINIMIZE ? 1 : icon == I_MAXIMIZE ? 2 : 3;
    }
}

static bool TabBtn(Clay_ElementId id, const char *label, bool sel, float size, float rad, bool pill) {
    Bs b = Interact(id);
    float s = Anim(id.id + 7, sel ? 1.f : 0.f, 45.f);
    Clay_Color bg;
    uint16_t bw = 0;
    Clay_Color bc = RGBA(0, 0, 0, 0);
    if (pill) {
        /* the selected background is the sliding pill drawn by the segment itself;
           no press flash, no hover/press tint on the right-panel tabs */
        bg = RGBA(255, 255, 255, 0);
    } else {
        float a = fmaxf(s * 255.f, b.hover * (1.f - s) * 60.f);
        Clay_Color fill = RGB(44, 46, 52);
        bg = Mix(RGBA(fill.r, fill.g, fill.b, a), RGBA(90, 95, 106, 255), b.press * 0.55f);
        bw = 1;
        bc = RGBA(66, 68, 76, 255.f * s);
    }
    Clay_Color off = pill ? C_ICON : C_MUTED;
    CLAY(id, {.layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(rad),
              .border = {.color = bc, .width = {.left = bw, .right = bw, .top = bw, .bottom = bw}}}) {
        Tx(label, size, Mix(off, C_TEXT, fmaxf(s, b.hover * 0.6f)), 0);
    }
    /* select on press, not release, so it feels instant */
    return Clay_PointerOver(id) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* link / chain toggle: bright chain when off, blue chain + soft blue glow when linked */
static bool LinkBtn(Clay_ElementId id, bool linked, float box, float isz) {
    Bs b = Interact(id);
    float la = Anim(id.id + 11u, linked ? 1.f : 0.f, 16.f);
    Clay_Color off = Mix(RGB(226, 227, 231), RGB(255, 255, 255), b.hover);
    Clay_Color ic = Dark(Mix(off, RGB(59, 130, 246), la), b.press * 0.2f);
    CLAY(id, {.layout = {.sizing = {FIX(box), FIX(box)}, .childAlignment = AL_C},
              .custom = {.customData = NC(K_GLOW, RGB(59, 130, 246), RGB(59, 130, 246), RGB(59, 130, 246), la,
                                          14.f * b.hover + 56.f * b.press, 0)}}) {
        Icon(I_LINK, isz, ic);
    }
    return b.clicked;
}


/* Shared wheel and media definitions used by panels and renderer. */
/* ------------------------------------------------------------------ */
/* wheel helpers                                                       */
/* ------------------------------------------------------------------ */
static void WheelInfo(Vector2 p, float *hue, float *len) {
    *len = sqrtf(p.x * p.x + p.y * p.y);
    *hue = fmodf(atan2f(p.y, p.x) * RAD2DEG + 90.f + 360.f, 360.f);
}
