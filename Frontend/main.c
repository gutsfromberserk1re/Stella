/*
 * Pluto - native raylib editor UI backed by the Rust C ABI
 * Layout: Clay (https://github.com/nicbarker/clay, v0.14 API)
 * Renderer: raylib (custom renderer included below, no external clay renderer needed)
 *
 * Text rendering: every font size is rasterised at its exact pixel size (cached), so glyphs
 * are drawn 1:1 with no texture down-scaling -> crisp text.
 *
 * Optional Inter-Regular, Inter-Medium, and Inter-SemiBold files can live in resources/.
 * Raster size follows monitor DPI; logical layout sizes remain unchanged.
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define Rectangle    Win32Rectangle
#define CloseWindow  Win32CloseWindow
#define ShowCursor   Win32ShowCursor
#define Arc          Win32Arc
#define DrawText     Win32DrawText
#define DrawTextEx   Win32DrawTextEx
#define DrawIcon     Win32DrawIcon
#define LoadImage    Win32LoadImage
#define PlaySound    Win32PlaySound
#include <windows.h>
#include <windowsx.h>
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#undef Arc
#undef DrawText
#undef DrawTextEx
#undef DrawIcon
#undef LoadImage
#undef PlaySound
#undef RGB
#endif

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "raylib.h"

/* ------------------------------------------------------------------ */
/* Compatibility helpers: legacy CLAY(id, { config }) call style       */
/* ------------------------------------------------------------------ */
static uint32_t gClayAutoId = 0;

static Clay_ElementDeclaration ClayCompatDeclaration(
    Clay_ElementId id,
    Clay_ElementDeclaration declaration
) {
    declaration.id = id;
    return declaration;
}

#undef CLAY
#define CLAY(id, ...) \
    for ( \
        CLAY__ELEMENT_DEFINITION_LATCH = ( \
            Clay__OpenElement(), \
            Clay__ConfigureOpenElement( \
                ClayCompatDeclaration((id), (Clay_ElementDeclaration)__VA_ARGS__) \
            ), \
            0 \
        ); \
        CLAY__ELEMENT_DEFINITION_LATCH < 1; \
        CLAY__ELEMENT_DEFINITION_LATCH = 1, Clay__CloseElement() \
    )

#define CLAY_AUTO_ID(...) \
    CLAY(CLAY_IDI_LOCAL("PlutoAuto", gClayAutoId++), __VA_ARGS__)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>


/*
 * Pluto frontend entry point.
 * This remains one translation unit intentionally: the headers contain
 * static implementation/state and are included exactly once in this order.
 */
#include "core.h"
#include "backend_bridge.h"
#include "draw.h"

/* Layout helpers and top bar are kept in the entry point. */
/* Shared panel layout, resizing and top-bar UI. */
/* ------------------------------------------------------------------ */
/* resizable panels                                                    */
/* ------------------------------------------------------------------ */
static const char *kSplitKey[3] = {"SplitL", "SplitR", "SplitB"};

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void ClampPanels(void) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    /* root padding 16 + two 10px splitters = 36 */
    gRightW = Clampf(gRightW, RIGHT_MIN, fmaxf(RIGHT_MIN, fminf(RIGHT_MAX, W - 36.f - LEFT_MIN - CENTER_MIN)));
    gLeftW  = Clampf(gLeftW,  LEFT_MIN,  fmaxf(LEFT_MIN,  fminf(LEFT_MAX,  W - 36.f - gRightW - CENTER_MIN)));
    /* top bar 48 + 10 gap + 10 splitter + 12 bottom pad (+2 slack) */
    gTimeH  = Clampf(gTimeH,  TL_MIN,    fmaxf(TL_MIN,    fminf(TL_MAX,    H - 82.f - MAIN_MIN)));
}

/* runs before layout so the new size applies in the same frame */
static void UpdateSplits(void) {
    Vector2 m = GetMousePosition();
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) gSplit = 0;
    else if (gSplit == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int i = 0; i < 3; i++) {
            if (Clay_PointerOver(CLAY_SIDI(S(kSplitKey[i]), 0))) {
                gSplit = i + 1;
                gSplitM = i == 2 ? m.y : m.x;
                gSplitV = i == 0 ? gLeftW : i == 1 ? gRightW : gTimeH;
            }
        }
    }
    if (gSplit == 1) gLeftW  = gSplitV + (m.x - gSplitM);
    if (gSplit == 2) gRightW = gSplitV - (m.x - gSplitM);
    if (gSplit == 3) gTimeH  = gSplitV - (m.y - gSplitM);
    ClampPanels();
}

/* which: 0 left|center, 1 center|right, 2 main/timeline */
static void Splitter(int which) {
    Clay_ElementId id = CLAY_SIDI(S(kSplitKey[which]), 0);
    bool vert = which < 2;
    bool over = Clay_PointerOver(id);
    bool act  = gSplit == which + 1;
    if (act || (over && !gSplit)) gResize = vert ? 1 : 2;
    float v = Anim(id.id, act ? 1.f : (over && !gSplit) ? .55f : 0.f, 22.f);
    CLAY(id, {.layout = {.sizing = {vert ? CLAY_SIZING_FIXED(10) : GROW, vert ? GROW : CLAY_SIZING_FIXED(10)}},
              .custom = {.customData = NC(K_SPLIT, C_ACCENT, C_ACCENT, C_ACCENT, v, 0, vert)}}) {}
}

static void PlutoApplyKeyframeValues(void);

static void TimelineHistoryAction(bool redo) {
    PlutoTextAutoUpdateFlush();
    PlutoTimelineEndPropertyEdit();
    if (PlutoTimelineHistory(redo)) {
        PlutoApplyKeyframeValues();
        gPreviewDirty = true;
        gPreviewDebounce = 0.f;
    }
}

/* Native multi-clip audio clock. Media inspection creates one cached OGG
   playback proxy per source, so MP4/MOV and detached audio clips all use the
   same Raylib streaming path. Streams are reused across play/pause/scrub. */
#define STELLA_AUDIO_VOICES 32
#if defined(_WIN32)
#define STELLA_AUDIO_PATH_COPY(dst, src) strncpy((dst), (src), sizeof(dst) - 1)
#else
#define STELLA_AUDIO_PATH_COPY(dst, src) strncpy((dst), (src), sizeof(dst) - 1)
#endif
typedef struct {
    char clipId[64];
    char path[1024];
    Music music;
    bool loaded;
    bool failed;
    bool wasActive;
} StellaAudioVoice;
static StellaAudioVoice gAudioVoices[STELLA_AUDIO_VOICES];
static bool gAudioReady;
static float gAudioMixHeadroom = 1.f;

static void AudioVoiceRelease(StellaAudioVoice *voice) {
    if (!voice) return;
    if (voice->loaded) {
        if (IsMusicStreamPlaying(voice->music)) StopMusicStream(voice->music);
        UnloadMusicStream(voice->music);
    }
    memset(voice, 0, sizeof(*voice));
}

static StellaAudioVoice *AudioVoiceForClip(const TimelineClipUi *clip) {
    const char *path = clip->audioPath[0] ? clip->audioPath :
        ((clip->track >= 4 || clip->hasAudio) ? clip->path : "");
    if ((!path || !*path || !FileExists(path)) && clip->path[0] && FileExists(clip->path))
        path = clip->path;
    if (!path || !*path || !FileExists(path)) return NULL;
    StellaAudioVoice *freeVoice = NULL;
    for (int i = 0; i < STELLA_AUDIO_VOICES; i++) {
        StellaAudioVoice *voice = &gAudioVoices[i];
        if ((voice->loaded || voice->failed) && strcmp(voice->clipId, clip->id) == 0) {
            if (strcmp(voice->path, path) == 0) {
                if (voice->failed) { voice->wasActive = true; return NULL; }
                return voice;
            }
            AudioVoiceRelease(voice);
            freeVoice = voice;
            break;
        }
        if (!voice->loaded && !voice->failed && !freeVoice) freeVoice = voice;
    }
    if (!freeVoice) {
        /* Reclaim an inactive cached stream before giving up on a new voice. */
        for (int i = 0; i < STELLA_AUDIO_VOICES; i++) {
            StellaAudioVoice *voice = &gAudioVoices[i];
            if (!voice->wasActive) { AudioVoiceRelease(voice); freeVoice = voice; break; }
        }
    }
    if (!freeVoice) return NULL;
    STELLA_AUDIO_PATH_COPY(freeVoice->clipId, clip->id);
    STELLA_AUDIO_PATH_COPY(freeVoice->path, path);
    freeVoice->music = LoadMusicStream(path);
    if (!freeVoice->music.stream.buffer && clip->path[0] && strcmp(path, clip->path) != 0 && FileExists(clip->path)) {
        /* A stale or unavailable proxy should not permanently silence a source
           that Raylib can decode directly (WAV/MP3/OGG/WebM audio, etc.). */
        if (freeVoice->music.stream.buffer) UnloadMusicStream(freeVoice->music);
        memset(&freeVoice->music, 0, sizeof(freeVoice->music));
        path = clip->path;
        STELLA_AUDIO_PATH_COPY(freeVoice->path, path);
        freeVoice->music = LoadMusicStream(path);
    }
    if (!freeVoice->music.stream.buffer) {
        freeVoice->failed = true;
        freeVoice->wasActive = true;
        snprintf(gBackendMessage, sizeof(gBackendMessage), "Audio stream could not be opened: %.160s", path);
        return NULL;
    }
    freeVoice->loaded = true;
    freeVoice->wasActive = false;
    SetMusicVolume(freeVoice->music, 0.f);
    return freeVoice;
}

static void AudioPlaybackTick(bool playing) {
    if (!gAudioReady || !IsAudioDeviceReady()) return;
    bool active[STELLA_AUDIO_VOICES] = {false};
    float mixGainTotal = 0.f;
    for (int i = 0; i < gTimelineClipCount; i++) {
        TimelineClipUi *clip = &gTimelineClips[i];
        if (!(clip->track >= 4 || clip->hasAudio) || clip->duration <= 0.f ||
            gPlayhead < clip->start - .0005f || gPlayhead >= clip->start + clip->duration - .0005f) continue;
        StellaAudioVoice *voice = AudioVoiceForClip(clip);
        if (!voice || !voice->loaded) continue;
        int slot = (int)(voice - gAudioVoices);
        if (slot < 0 || slot >= STELLA_AUDIO_VOICES) continue;
        active[slot] = true;
        voice->wasActive = true;

        float sourceTime = fmaxf(0.f, clip->sourceOffset + gPlayhead - clip->start);
        float fade = 1.f;
        if (clip->audioFadeIn && clip->audioFadeInDuration > .001f)
            fade = fminf(fade, fmaxf(0.f, (gPlayhead - clip->start) / clip->audioFadeInDuration));
        float remaining = clip->start + clip->duration - gPlayhead;
        if (clip->audioFadeOut && clip->audioFadeOutDuration > .001f)
            fade = fminf(fade, fmaxf(0.f, remaining / clip->audioFadeOutDuration));
        float volume = fmaxf(0.f, fminf(2.f, clip->audioVolume / 100.f)) * fade;
        float pan = fmaxf(-1.f, fminf(1.f, clip->audioPan / 100.f));
        mixGainTotal += volume;
        SetMusicVolume(voice->music, volume);
        SetMusicPan(voice->music, pan);

        bool streamPlaying = IsMusicStreamPlaying(voice->music);
        if (playing) {
            if (!streamPlaying) {
                SeekMusicStream(voice->music, sourceTime);
                PlayMusicStream(voice->music);
            }
            /* Raylib's streaming music buffers must be pumped on the same frame
               that PlayMusicStream starts, then once per UI frame thereafter. */
            UpdateMusicStream(voice->music);
            float actual = GetMusicTimePlayed(voice->music);
            /* Keep audio aligned with the timeline clock; a 220ms tolerance was
               visibly late on cuts and after scrubbing. Avoid tiny seeks that
               can cause audible zippering, but correct meaningful drift. */
            if (fabsf(actual - sourceTime) > .10f) SeekMusicStream(voice->music, sourceTime);
        } else {
            if (streamPlaying) PauseMusicStream(voice->music);
            float actual = GetMusicTimePlayed(voice->music);
            if (fabsf(actual - sourceTime) > .04f) SeekMusicStream(voice->music, sourceTime);
        }
    }
    /* A simple master-bus gain stage keeps overlapping full-level clips from
       summing into hard digital clipping. Smooth the gain so track entrances
       do not cause an audible volume jump. */
    float targetHeadroom = 1.f / fmaxf(1.f, mixGainTotal);
    float mixFollow = 1.f - expf(-8.f * fmaxf(0.f, gDt));
    gAudioMixHeadroom += (targetHeadroom - gAudioMixHeadroom) * mixFollow;
    SetMasterVolume(.92f * gAudioMixHeadroom);

    for (int i = 0; i < STELLA_AUDIO_VOICES; i++) {
        StellaAudioVoice *voice = &gAudioVoices[i];
        if (!voice->loaded && !voice->failed) continue;
        bool clipStillExists = false;
        for (int clipIndex = 0; clipIndex < gTimelineClipCount; clipIndex++) {
            if (strcmp(gTimelineClips[clipIndex].id, voice->clipId) == 0) { clipStillExists = true; break; }
        }
        if (!clipStillExists) { AudioVoiceRelease(voice); continue; }
        if (voice->loaded && !active[i] && IsMusicStreamPlaying(voice->music)) PauseMusicStream(voice->music);
        voice->wasActive = active[i] || voice->failed;
    }
}

static void AudioPlaybackShutdown(void) {
    for (int i = 0; i < STELLA_AUDIO_VOICES; i++) AudioVoiceRelease(&gAudioVoices[i]);
    if (gAudioReady && IsAudioDeviceReady()) CloseAudioDevice();
    gAudioReady = false;
}

/* ------------------------------------------------------------------ */
/* UI: top bar                                                         */
/* ------------------------------------------------------------------ */
static void TopBar(void) {
    CLAY(CLAY_ID("TopBar"), {.layout = {.sizing = {GROW, FIX(48)}, .padding = {12, 12, 0, 0}, .childAlignment = AL_Y}}) {
        CLAY(CLAY_ID("LeftCluster"), {.layout = {.sizing = {FIX(316), FIT}, .childAlignment = AL_Y}}) {
            CLAY(CLAY_ID("Logo"), {.layout = {.sizing = {FIX(34), FIX(34)}},
                                    .custom = {.customData = NC(K_APP_LOGO, C_TEXT, C_TEXT, C_CARD, 0, 0, 0)}}) {}
            GAPX(14)
            Tx("Stella", 17, C_TEXT, 0);
            GAPX(40)
            Tx("Untitled Project", 15, C_DIM, 0);
        }
        Clay_ElementId ex = CLAY_ID("Export");
        Bs b = Interact(ex);
        if (b.clicked) PlutoRequestExport();
        CLAY(ex, {.layout = {.sizing = {FIT, FIX(32)}, .padding = {12, 14, 0, 0}, .childGap = 8, .childAlignment = AL_Y},
                  .backgroundColor = BtnBg(RGB(27, 28, 33), RGB(38, 40, 46), b), .cornerRadius = CLAY_CORNER_RADIUS(9),
                  .border = {.color = Mix(RGB(54, 56, 62), RGB(86, 89, 98), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Icon(I_EXPORT, 14, C_TEXT);
            Tx("Export", 14, C_TEXT, 0);
        }
        if (gBackendMessage[0]) {
            GAPX(10)
            CLAY(CLAY_ID("BackendStatus"), {.layout = {.sizing = {FIX(300), FIT}}}) {
                Tx(gBackendMessage, 12, C_MUTED, 0);
            }
        }
        HSPACE
        WindowButton("WindowMinimize", I_MINIMIZE);
        WindowButton("WindowMaximize", I_MAXIMIZE);
        WindowButton("WindowClose", I_CLOSE);
    }
}


#include "left_panel.h"
#include "center_panel.h"
#include "right_panel.h"
#include "timeline.h"

/* ------------------------------------------------------------------ */
/* frame                                                               */
/* ------------------------------------------------------------------ */
static Clay_RenderCommandArray BuildUI(void) {
    gClayAutoId = 0;
    gFbN = 0;
    TickState();
    UpdateSplits();
    UpdateInspScroll();
    Clay_BeginLayout();
    CLAY(CLAY_ID("Root"), {.layout = {COLM, .sizing = {GROW, GROW}, .padding = {8, 8, 0, 12}}, .backgroundColor = C_BG}) {
        TopBar();
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, CLAY_SIZING_FIXED(10)}}}) {}
        CLAY(CLAY_ID("Main"), {.layout = {.sizing = {GROW, GROW}}}) {
            LeftPanel();
            Splitter(0);
            CenterPanel();
            Splitter(1);
            RightPanel();
        }
        Splitter(2);
        Timeline();
    }
    return Clay_EndLayout();
}

/* unitsPerEm / (hhea ascender - descender) of a TTF.
   raylib/stb size a font so that (ascender - descender) == fontSize, so the real em height
   is fontSize * this ratio; Segoe UI gives 0.752, Inter 0.826, DejaVu 0.86 ... */
static float FontEmRatio(const char *path) {
    int n = 0;
    unsigned char *d = LoadFileData(path, &n);
    float k = 0.f;
    if (d && n > 64) {
#define BE16(p) ((unsigned)(((p)[0] << 8) | (p)[1]))
        int nt = (int)BE16(d + 4);
        unsigned upm = 0; int asc = 0, desc = 0;
        for (int i = 0; i < nt && 12 + 16 * (i + 1) <= n; i++) {
            unsigned char *rec = d + 12 + 16 * i;
            unsigned off = ((unsigned)rec[8] << 24) | ((unsigned)rec[9] << 16) | ((unsigned)rec[10] << 8) | (unsigned)rec[11];
            if ((long long)off + 20 > n) continue;
            if (memcmp(rec, "head", 4) == 0) upm = BE16(d + off + 18);
            else if (memcmp(rec, "hhea", 4) == 0) { asc = (short)BE16(d + off + 4); desc = (short)BE16(d + off + 6); }
        }
#undef BE16
        if (upm && asc - desc > 0) k = (float)upm / (float)(asc - desc);
    }
    if (d) UnloadFileData(d);
    return k;
}

/* pick the first existing font file; atlases are rasterised lazily per exact pixel size */
static bool FindFont(const char **paths, int n, int slot) {
    for (int i = 0; i < n; i++) {
        if (FileExists(paths[i])) {
            /* every size in this file was tuned on Segoe UI (ratio 0.752); keep the same em for any other font */
            float er = FontEmRatio(paths[i]);
            gFK[slot] = (er > 0.3f && er < 1.3f) ? 0.752f / er : 1.f;
            strncpy(gFontPath[slot], paths[i], sizeof(gFontPath[slot]) - 1);
            gFontPath[slot][sizeof(gFontPath[slot]) - 1] = 0;
            gFontOK[slot] = true;
            return true;
        }
    }
    gFK[slot] = 1.f;
    return false;
}

#ifdef _WIN32
static LRESULT CALLBACK PlutoWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST && gPreviousWndProc) {
        LRESULT base = CallWindowProc(gPreviousWndProc, hwnd, msg, wParam, lParam);
        if (base != HTCLIENT) return base;

        POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &p);
        RECT r; GetClientRect(hwnd, &r);

        if (!IsZoomed(hwnd)) {
            const int edge = 7;
            bool left = p.x < edge, right = p.x >= r.right - edge;
            bool top = p.y < edge, bottom = p.y >= r.bottom - edge;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }

        /* The empty middle of the custom top bar behaves like a normal title bar. */
        if (p.y >= 7 && p.y < 50 && p.x >= 450 && p.x < r.right - 132) return HTCAPTION;
        return HTCLIENT;
    }
    return gPreviousWndProc ? CallWindowProc(gPreviousWndProc, hwnd, msg, wParam, lParam)
                            : DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif

int main(void) {
#ifdef _WIN32
    /* Prevent Windows bitmap-scaling the UI on scaled displays. */
    SetProcessDPIAware();
#endif
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(1600, 967, "Stella");   /* borderless custom title bar; 967 px tall => Transform fits */
    InitAudioDevice();
    gAudioReady = IsAudioDeviceReady();
    if (gAudioReady) SetMasterVolume(.92f);
    else snprintf(gBackendMessage, sizeof(gBackendMessage), "Audio device unavailable; check Windows output device");
    Vector2 dpiScale = GetWindowScaleDPI();
    gDpiScale = fmaxf(1.f, fminf(3.f, fmaxf(dpiScale.x, dpiScale.y)));
    LoadUiIcons();
    LoadKnobTexture();
    if (PlutoBackendLoad()) PlutoTimelineCall("{\"command\":\"timeline_get\"}");
    SetWindowMinSize(1180, 720);
#ifdef _WIN32
    HWND hwnd = (HWND)GetWindowHandle();
    if (hwnd) {
        HICON appIcon = (HICON)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
        if (appIcon) {
            SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)appIcon);
            SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)appIcon);
        }
        gPreviousWndProc = (WNDPROC)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
        if (gPreviousWndProc) SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)PlutoWindowProc);
    }
#endif

    const char *reg[] = {"resources/Inter-Regular.ttf", "Inter-Regular.ttf", "C:/Windows/Fonts/segoeui.ttf",
                         "/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    const char *medium[] = {"resources/Inter-Medium.ttf", "Inter-Medium.ttf", "C:/Windows/Fonts/seguisb.ttf",
                            "C:/Windows/Fonts/segoeuisl.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
                            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                            "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    const char *semiBold[] = {"resources/Inter-SemiBold.ttf", "Inter-SemiBold.ttf", "C:/Windows/Fonts/segoeuisb.ttf",
                          "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                          "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
    gFcpN = 0;
    for (int c = 32; c < 127; c++) gFcps[gFcpN++] = c;
    gFcps[gFcpN++] = 0xB0; gFcps[gFcpN++] = 0xD7; gFcps[gFcpN++] = 0xB7; gFcps[gFcpN++] = 0x2026;
    bool okR = FindFont(reg, 6, 0);
    bool okM = FindFont(medium, 7, 1);
    bool okSB = FindFont(semiBold, 8, 2);
    if (!okM && okR) {
        strcpy(gFontPath[1], gFontPath[0]);
        gFontOK[1] = true;
        gFK[1] = gFK[0];
    }
    if (!okSB && (okM || okR)) {
        int source = okM ? 1 : 0;
        strcpy(gFontPath[2], gFontPath[source]);
        gFontOK[2] = true;
        gFK[2] = gFK[source];
    }

    /* Title typography has its own small, cross-platform font set. Keep the
       existing three UI weights untouched; title slots are 3..6 regular and
       7..10 bold so inspector typography never changes the application UI. */
    if (okR) { strcpy(gFontPath[3], gFontPath[0]); gFontOK[3] = true; gFK[3] = gFK[0]; }
    if (okSB) { strcpy(gFontPath[7], gFontPath[2]); gFontOK[7] = true; gFK[7] = gFK[2]; }
    const char *arialRegular[] = {
        "C:/Windows/Fonts/arial.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };
    const char *arialBold[] = {
        "C:/Windows/Fonts/arialbd.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
    };
    const char *georgiaRegular[] = {
        "C:/Windows/Fonts/georgia.ttf", "/System/Library/Fonts/Supplemental/Georgia.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Georgia.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
    };
    const char *georgiaBold[] = {
        "C:/Windows/Fonts/georgiab.ttf", "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Georgia_Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSerif-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
    };
    const char *monoRegular[] = {
        "C:/Windows/Fonts/consola.ttf", "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf"
    };
    const char *monoBold[] = {
        "C:/Windows/Fonts/consolab.ttf", "/System/Library/Fonts/Supplemental/Courier New Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Bold.ttf"
    };
    FindFont(arialRegular, (int)(sizeof(arialRegular) / sizeof(arialRegular[0])), 4);
    FindFont(georgiaRegular, (int)(sizeof(georgiaRegular) / sizeof(georgiaRegular[0])), 5);
    FindFont(monoRegular, (int)(sizeof(monoRegular) / sizeof(monoRegular[0])), 6);
    FindFont(arialBold, (int)(sizeof(arialBold) / sizeof(arialBold[0])), 8);
    FindFont(georgiaBold, (int)(sizeof(georgiaBold) / sizeof(georgiaBold[0])), 9);
    FindFont(monoBold, (int)(sizeof(monoBold) / sizeof(monoBold[0])), 10);
    for (int i = 0; i < 4; i++) {
        int regularSlot = 3 + i, boldSlot = 7 + i;
        if (!gFontOK[regularSlot]) {
            int source = gFontOK[3] ? 3 : 0;
            strcpy(gFontPath[regularSlot], gFontPath[source]);
            gFontOK[regularSlot] = gFontOK[source];
            gFK[regularSlot] = gFK[source];
        }
        if (!gFontOK[boldSlot]) {
            int source = gFontOK[7] ? 7 : (gFontOK[regularSlot] ? regularSlot : 0);
            strcpy(gFontPath[boldSlot], gFontPath[source]);
            gFontOK[boldSlot] = gFontOK[source];
            gFK[boldSlot] = gFK[source];
        }
    }

    for (int i = 0; i < NSL; i++) gSl[i] = gSlT[i] = SlDefault(i);

    uint64_t mem = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(mem, malloc(mem));
    Clay_Initialize(arena, (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()}, (Clay_ErrorHandler){OnClayError, 0});
    Clay_SetMeasureTextFunction(MeasureTxt, NULL);

    int observedPreviewRes = gPreviewRes;
    int observedPreviewFps = gPreviewFps;
    float observedPlayhead = gPlayhead;
    float playbackRemainder = 0.f;
    float navigationRepeat = 0.f;
    while (!WindowShouldClose() && !gWantQuit) {
        gDt = GetFrameTime(); if (gDt > 0.05f) gDt = 0.05f;
        if (gPreviewRes != observedPreviewRes) {
            observedPreviewRes = gPreviewRes;
            gPreviewDirty = true;
            gPreviewDebounce = 1.f;
        }
        if (gPreviewFps != observedPreviewFps) {
            observedPreviewFps = gPreviewFps;
            playbackRemainder = 0.f;
        }
        if (fabsf(gPlayhead - observedPlayhead) > 0.0001f) {
            observedPlayhead = gPlayhead;
            if (!gTimelinePropertyEditOpen && gKeyframeDragClip < 0 && !gValueDragging && !gRotDrag && !gSlDrag[0]) {
                PlutoApplyKeyframeValues();
            }
            if (!gTimelinePlaying && !gTimelineScrubbing) {
                gPreviewDirty = true;
                gPreviewDebounce = 1.f;
            }
        }
        if (IsKeyPressed(KEY_SPACE) && gEditValueId == 0 && !gTextInputFocused) gTimelinePlaying = !gTimelinePlaying;
        if (IsKeyPressed(KEY_F3)) gPerfOverlay = !gPerfOverlay;
        gCtrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        gShiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        if (gEditValueId == 0 && !gTextInputFocused) {
            if (gCtrlDown && IsKeyPressed(KEY_Z)) TimelineHistoryAction(gShiftDown);
            else if (gCtrlDown && IsKeyPressed(KEY_Y)) TimelineHistoryAction(true);
            else if (!gCtrlDown && !altDown) {
                if (IsKeyPressed(KEY_V)) { gTimelineTool = 0; gTextInputFocused = false; gTextFocusStartedAt = 0.0; }
                if (IsKeyPressed(KEY_H)) { gTimelineTool = 1; gTextInputFocused = false; gTextFocusStartedAt = 0.0; }
                if (IsKeyPressed(KEY_C)) { gTimelineTool = 2; gTextInputFocused = false; gTextFocusStartedAt = 0.0; }
                if (IsKeyPressed(KEY_T)) { gTimelineTool = 3; gRightTab = 3; gTextInputFocused = false; gTextFocusStartedAt = 0.0; }
                if (IsKeyPressed(KEY_S)) PlutoTimelineSnap(!gTimelineSnap);
            }
        }
        if (gEditValueId == 0 && !gTextInputFocused && !gTimelinePlaying && IsKeyPressed(KEY_K) &&
            (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))) {
            int splitCount = 0;
            for (int i = 0; i < gTimelineClipCount; i++) {
                if (gTimelineMultiSelect[i] && gPlayhead > gTimelineClips[i].start + .001f &&
                    gPlayhead < gTimelineClips[i].start + gTimelineClips[i].duration - .001f) {
                    PlutoTimelineSplit(i, gPlayhead);
                    splitCount++;
                }
            }
            if (!splitCount && gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount &&
                gPlayhead > gTimelineClips[gTimelineSelected].start + .001f &&
                gPlayhead < gTimelineClips[gTimelineSelected].start + gTimelineClips[gTimelineSelected].duration - .001f)
                PlutoTimelineSplit(gTimelineSelected, gPlayhead);
        }
        if (gEditValueId == 0 && !gTextInputFocused && !gTimelinePlaying && IsKeyPressed(KEY_DELETE) &&
            gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
            /* A focused/selected keyframe takes priority over clip deletion.
               Clicking a diamond selects it; placing the playhead over one is
               also enough to target it, matching the toolbar's add/remove toggle. */
            int keyIndex = TimelineSelectedKeyframeIndex(gTimelineSelected);
            if (keyIndex < 0) keyIndex = TimelineKeyframeAtPlayhead(gTimelineSelected);
            if (keyIndex >= 0) {
                float keyTime = gTimelineClips[gTimelineSelected].keyframes[keyIndex].time;
                PlutoTimelineDeleteKeyframe(gTimelineSelected, keyTime);
            } else {
                PlutoTimelineDelete(gTimelineSelected);
                memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                gTimelineSelected = -1;
                gSelectedKeyframeClip = -1;
                gSelectedKeyframeTime = -1.f;
            }
        }
        bool navLeft = IsKeyDown(KEY_LEFT), navRight = IsKeyDown(KEY_RIGHT);
        bool navActive = gEditValueId == 0 && !gTextInputFocused && !gTimelinePlaying && (navLeft || navRight);
        gPreviewNavigating = navActive;
        bool navStep = false;
        if (navActive) {
            bool pressed = navLeft ? IsKeyPressed(KEY_LEFT) : IsKeyPressed(KEY_RIGHT);
            if (pressed) { navigationRepeat = 0.f; navStep = true; }
            else {
                navigationRepeat += gDt;
                if (navigationRepeat >= .24f) {
                    navigationRepeat -= 1.f / (float)fmaxf(1, gPreviewFps);
                    navStep = true;
                }
            }
        } else navigationRepeat = 0.f;
        if (navStep) {
            const float stepFps = (float)fmaxf(1, gPreviewFps);
            float frame = floorf(gPlayhead * stepFps + .5f);
            frame += navLeft ? -1.f : 1.f;
            gPlayhead = fmaxf(0.f, fminf(TimelineDuration(), frame / stepFps));
            gPreviewDirty = true;
            gPreviewDebounce = 1.f;
        }
        if (gTimelinePlaying) {
            float end = TimelineDuration();
            playbackRemainder += gDt;
            const float frameDuration = 1.f / (float)fmaxf(1, gPreviewFps);
            if (playbackRemainder >= frameDuration) {
                float advance = floorf(playbackRemainder / frameDuration) * frameDuration;
                playbackRemainder -= advance;
                gPlayhead += advance;
                if (gPlayhead >= end) {
                    gPlayhead = end; gTimelinePlaying = false; playbackRemainder = 0.f;
                }
                gPreviewDirty = true;
            }
        } else playbackRemainder = 0.f;
        AudioPlaybackTick(gTimelinePlaying);
        gWheelDelta = GetMouseWheelMove();
        gCtrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        gShiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        Vector2 currentDpi = GetWindowScaleDPI();
        float currentDpiScale = fmaxf(1.f, fminf(3.f, fmaxf(currentDpi.x, currentDpi.y)));
        if (fabsf(currentDpiScale - gDpiScale) > 0.01f) gDpiScale = currentDpiScale;
        Clay_SetLayoutDimensions((Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()});
        Vector2 m = GetMousePosition();
        Clay_SetPointerState((Clay_Vector2){m.x, m.y}, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        Clay_BoundingBox timeBodyForWheel = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox;
        bool shiftTimelineWheel = gShiftDown && !gCtrlDown && timeBodyForWheel.width > 0.f &&
            CheckCollisionPointRec(m, (Rectangle){timeBodyForWheel.x, timeBodyForWheel.y,
                                                  timeBodyForWheel.width, timeBodyForWheel.height});
        Clay_UpdateScrollContainers(false, (Clay_Vector2){0, (gCtrlDown || shiftTimelineWheel) ? 0.f : gWheelDelta * 28.f}, gDt);

        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) gDragId = 0;
        PlutoPollBackend();
        gCustN = 0; gHand = false; gCross = false; gResize = 0; gScrubCursor = false;
        gMediaHoverName[0] = 0;
        Clay_RenderCommandArray cmds = BuildUI();
        if (gTimelinePropertyEditOpen && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            !gTextAutoUpdatePending && gTextAutoUpdateCommitAt <= 0.0) {
            PlutoTimelineEndPropertyEdit();
        }
        PlutoTextAutoUpdateTick();
        if (gPreviewDirty && !gPreviewTaskId) {
            gPreviewDebounce += gDt;
            bool responsiveEdit = gTimelinePropertyEditOpen || gKeyframeDragClip >= 0 ||
                gValueDragging || gRotDrag || gSlDrag[0];
            if (gPreviewDebounce >= (gTimelineScrubbing || gTimelinePlaying || gPreviewNavigating || responsiveEdit ? 0.f : .075f)) {
                if (PlutoRequestPreview()) { gPreviewDirty = false; gPreviewDebounce = 0.f; }
                else { gPreviewDebounce = .045f; }
            }
        } else gPreviewDebounce = 0.f;

        if (gWindowAction == 1) MinimizeWindow();
        else if (gWindowAction == 2) {
            if (IsWindowState(FLAG_WINDOW_MAXIMIZED)) RestoreWindow();
            else MaximizeWindow();
        }
        else if (gWindowAction == 3) gWantQuit = true;
        gWindowAction = 0;
        if (gWantQuit) break;

        BeginDrawing();
        ClearBackground((Color){14, 15, 17, 255});
        RenderUI(cmds);
        DrawPreviewQualityDropdown();
        DrawPreviewFpsDropdown();
        DrawTimelineContextMenu();
        DrawPerfOverlay();
        EndDrawing();
        SetMouseCursor(gResize == 1 ? MOUSE_CURSOR_RESIZE_EW : gResize == 2 ? MOUSE_CURSOR_RESIZE_NS :
                       gCross ? MOUSE_CURSOR_CROSSHAIR : gScrubCursor ? MOUSE_CURSOR_RESIZE_EW :
                       gHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    }
    AudioPlaybackShutdown();
    PlutoUnloadMedia();
    UnloadUiIcons();
    CloseWindow();
    return 0;
}
