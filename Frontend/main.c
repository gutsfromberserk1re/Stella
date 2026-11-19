/*
 * Pluto - video editor UI (visual only, no backend)
 * Layout: Clay (https://github.com/nicbarker/clay, v0.14 API)
 * Renderer: raylib (custom renderer included below, no external clay renderer needed)
 *
 * Text rendering: every font size is rasterised at its exact pixel size (cached), so glyphs
 * are drawn 1:1 with no texture down-scaling -> crisp text.
 *
 * Font note: put Inter-Regular.ttf and Inter-SemiBold.ttf in ./resources/ for the exact
 * glyph shapes of the reference. Font sizes are normalised, so any font renders at the
 * same size.
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

/* ------------------------------------------------------------------ */
/* UI: top bar                                                         */
/* ------------------------------------------------------------------ */
static void TopBar(void) {
    CLAY(CLAY_ID("TopBar"), {.layout = {.sizing = {GROW, FIX(48)}, .padding = {12, 12, 0, 0}, .childAlignment = AL_Y}}) {
        CLAY(CLAY_ID("LeftCluster"), {.layout = {.sizing = {FIX(316), FIT}, .childAlignment = AL_Y}}) {
            CLAY(CLAY_ID("Logo"), {.layout = {.sizing = {FIX(24), FIX(24)}}, .backgroundColor = RGB(20, 21, 25),
                                   .cornerRadius = CLAY_CORNER_RADIUS(6), .border = {.color = RGB(64, 66, 73), .width = CLAY_BORDER_OUTSIDE(1)}}) {}
            GAPX(14)
            Tx("Pluto", 15, C_TEXT, 1);
            GAPX(40)
            Tx("Untitled Project", 13, C_DIM, 0);
        }
        Clay_ElementId ex = CLAY_ID("Export");
        Bs b = Interact(ex);
        CLAY(ex, {.layout = {.sizing = {FIT, FIX(32)}, .padding = {12, 14, 0, 0}, .childGap = 8, .childAlignment = AL_Y},
                  .backgroundColor = BtnBg(RGB(27, 28, 33), RGB(38, 40, 46), b), .cornerRadius = CLAY_CORNER_RADIUS(9),
                  .border = {.color = Mix(RGB(54, 56, 62), RGB(86, 89, 98), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Icon(I_EXPORT, 14, C_TEXT);
            Tx("Export", 13, C_TEXT, 1);
        }
        GAPX(18)
        IconBtn(CLAY_ID("Undo"), I_UNDO, 28, 15, C_DIM, false, 8);
        GAPX(8)
        IconBtn(CLAY_ID("Redo"), I_REDO, 28, 15, RGB(76, 79, 88), false, 8);
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
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 967, "Pluto");   /* borderless custom title bar; 967 px tall => Transform fits */
    SetWindowMinSize(1180, 720);
#ifdef _WIN32
    HWND hwnd = (HWND)GetWindowHandle();
    if (hwnd) {
        gPreviousWndProc = (WNDPROC)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
        if (gPreviousWndProc) SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)PlutoWindowProc);
    }
#endif

    const char *reg[] = {"resources/Inter-Regular.ttf", "Inter-Regular.ttf", "C:/Windows/Fonts/segoeui.ttf",
                         "/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    const char *bold[] = {"resources/Inter-SemiBold.ttf", "Inter-SemiBold.ttf", "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                          "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
    gFcpN = 0;
    for (int c = 32; c < 127; c++) gFcps[gFcpN++] = c;
    gFcps[gFcpN++] = 0xB0; gFcps[gFcpN++] = 0xD7; gFcps[gFcpN++] = 0xB7; gFcps[gFcpN++] = 0x2026;
    bool okR = FindFont(reg, 6, 0);
    bool okB = FindFont(bold, 7, 1);
    if (!okB && okR) {
        strcpy(gFontPath[1], gFontPath[0]);
        gFontOK[1] = true;
        gFK[1] = gFK[0];
    }

    for (int i = 0; i < NSL; i++) gSl[i] = gSlT[i] = SlDefault(i);

    uint64_t mem = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(mem, malloc(mem));
    Clay_Initialize(arena, (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()}, (Clay_ErrorHandler){OnClayError, 0});
    Clay_SetMeasureTextFunction(MeasureTxt, NULL);

    while (!WindowShouldClose() && !gWantQuit) {
        gDt = GetFrameTime(); if (gDt > 0.05f) gDt = 0.05f;
        Clay_SetLayoutDimensions((Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()});
        Vector2 m = GetMousePosition();
        Clay_SetPointerState((Clay_Vector2){m.x, m.y}, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        Clay_UpdateScrollContainers(false, (Clay_Vector2){0, GetMouseWheelMove() * 28.f}, gDt);

        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { gDragId = 0; gDragWheel = -1; }
        gCustN = 0; gHand = false; gCross = false; gResize = 0;
        Clay_RenderCommandArray cmds = BuildUI();

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
        EndDrawing();
        SetMouseCursor(gResize == 1 ? MOUSE_CURSOR_RESIZE_EW : gResize == 2 ? MOUSE_CURSOR_RESIZE_NS :
                       gCross ? MOUSE_CURSOR_CROSSHAIR : gHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    }
    CloseWindow();
    return 0;
}
