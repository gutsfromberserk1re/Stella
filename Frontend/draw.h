/* Pluto frontend drawing: raylib primitives, icons, custom elements and Clay renderer. */
/* ------------------------------------------------------------------ */
/* raylib drawing helpers                                              */
/* ------------------------------------------------------------------ */
static unsigned char U8(float x) { return (unsigned char)(x < 0 ? 0 : x > 255 ? 255 : x + .5f); }
static Color RC(Clay_Color c) { return (Color){U8(c.r), U8(c.g), U8(c.b), U8(c.a)}; }
static Color LerpC(Color a, Color b, float t) {
    return (Color){U8(a.r + (b.r - a.r) * t), U8(a.g + (b.g - a.g) * t), U8(a.b + (b.b - a.b) * t), U8(a.a + (b.a - a.a) * t)};
}
static Color MediaSwatch(const char *key, int fallback) {
    static const Color palette[] = {
        {91, 72, 126, 255}, {63, 88, 132, 255}, {57, 105, 117, 255},
        {62, 112, 86, 255}, {82, 76, 132, 255}, {67, 111, 101, 255},
        {75, 93, 133, 255}, {83, 114, 94, 255}
    };
    uint32_t hash = 2166136261u;
    if (key && *key) {
        for (const unsigned char *p = (const unsigned char *)key; *p; p++) hash = (hash ^ *p) * 16777619u;
    } else hash ^= (uint32_t)fallback;
    return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}
static Color ShadeMediaColor(Color color, float amount) {
    float scale = 1.f + amount;
    return (Color){U8(color.r * scale), U8(color.g * scale), U8(color.b * scale), color.a};
}

static void FillRR(Rectangle r, float rad, Color c) {
    if (c.a == 0 || r.width <= 0 || r.height <= 0) return;
    float m = fminf(r.width, r.height);
    if (rad <= 0.5f) { DrawRectangleRec(r, c); return; }
    DrawRectangleRounded(r, fminf(1.f, rad * 2.f / m), 20, c);
}
static void StrokeRR(Rectangle r, float rad, float t, Color c) {
    if (c.a == 0 || r.width <= 0 || r.height <= 0) return;
    rad = fminf(rad, fminf(r.width, r.height) / 2.f);
    if (rad < 1.f) {
        DrawRectangleRec((Rectangle){r.x, r.y, r.width, t}, c);
        DrawRectangleRec((Rectangle){r.x, r.y + r.height - t, r.width, t}, c);
        DrawRectangleRec((Rectangle){r.x, r.y + t, t, r.height - 2 * t}, c);
        DrawRectangleRec((Rectangle){r.x + r.width - t, r.y + t, t, r.height - 2 * t}, c);
        return;
    }
    DrawRectangleRec((Rectangle){r.x + rad, r.y, r.width - 2 * rad, t}, c);
    DrawRectangleRec((Rectangle){r.x + rad, r.y + r.height - t, r.width - 2 * rad, t}, c);
    DrawRectangleRec((Rectangle){r.x, r.y + rad, t, r.height - 2 * rad}, c);
    DrawRectangleRec((Rectangle){r.x + r.width - t, r.y + rad, t, r.height - 2 * rad}, c);
    DrawRing((Vector2){r.x + rad, r.y + rad},                     rad - t, rad, 180, 270, 16, c);
    DrawRing((Vector2){r.x + r.width - rad, r.y + rad},           rad - t, rad, 270, 360, 16, c);
    DrawRing((Vector2){r.x + r.width - rad, r.y + r.height - rad}, rad - t, rad, 0, 90, 16, c);
    DrawRing((Vector2){r.x + rad, r.y + r.height - rad},          rad - t, rad, 90, 180, 16, c);
}
static void DottedLine(Vector2 a, Vector2 b, float dash, float gap, float thickness, Color color) {
    Vector2 d = {b.x - a.x, b.y - a.y};
    float length = sqrtf(d.x * d.x + d.y * d.y);
    if (length <= .01f) return;
    Vector2 dir = {d.x / length, d.y / length};
    for (float at = 0.f; at < length; at += dash + gap) {
        float end = fminf(length, at + dash);
        DrawLineEx((Vector2){a.x + dir.x * at, a.y + dir.y * at},
                   (Vector2){a.x + dir.x * end, a.y + dir.y * end}, thickness, color);
    }
}
static void DrawDottedRect(Rectangle r, float dash, float gap, float thickness, Color color) {
    Vector2 tl = {r.x, r.y}, tr = {r.x + r.width, r.y};
    Vector2 br = {r.x + r.width, r.y + r.height}, bl = {r.x, r.y + r.height};
    DottedLine(tl, tr, dash, gap, thickness, color);
    DottedLine(tr, br, dash, gap, thickness, color);
    DottedLine(br, bl, dash, gap, thickness, color);
    DottedLine(bl, tl, dash, gap, thickness, color);
}
static void GradRR(Rectangle r, float rad, Color top, Color bot) {
    rad = fminf(rad, fminf(r.width, r.height) / 2.f);
    int h = (int)r.height;
    for (int i = 0; i < h; i++) {
        float y = i + 0.5f, inset = 0;
        if (y < rad)                 { float dy = rad - y;               inset = rad - sqrtf(fmaxf(0, rad * rad - dy * dy)); }
        else if (y > r.height - rad) { float dy = y - (r.height - rad);  inset = rad - sqrtf(fmaxf(0, rad * rad - dy * dy)); }
        DrawRectangle((int)(r.x + inset), (int)(r.y + i), (int)(r.width - 2 * inset), 1, LerpC(top, bot, (float)i / fmaxf(1, h - 1)));
    }
}
static void FilledCapsule(Vector2 a, Vector2 b, float w, Color col) {
    DrawLineEx(a, b, w, col);
    DrawCircleV(a, w * .5f, col);
    DrawCircleV(b, w * .5f, col);
}
static void Ring(Vector2 c, float r, float t, Color col) { DrawRing(c, fmaxf(0, r - t * .5f), r + t * .5f, 0, 360, 40, col); }
static void Arc(Vector2 c, float r, float t, float a0, float a1, Color col) { DrawRing(c, fmaxf(0, r - t * .5f), r + t * .5f, a0, a1, 32, col); }
static void Capsule(Vector2 c, float angDeg, float len, float rad, float t, Color col) {
    float a = angDeg * DEG2RAD;
    Vector2 d = {cosf(a), sinf(a)}, n = {-d.y, d.x};
    Vector2 e0 = {c.x - d.x * len / 2, c.y - d.y * len / 2}, e1 = {c.x + d.x * len / 2, c.y + d.y * len / 2};
    DrawLineEx((Vector2){e0.x + n.x * rad, e0.y + n.y * rad}, (Vector2){e1.x + n.x * rad, e1.y + n.y * rad}, t, col);
    DrawLineEx((Vector2){e0.x - n.x * rad, e0.y - n.y * rad}, (Vector2){e1.x - n.x * rad, e1.y - n.y * rad}, t, col);
    Arc(e1, rad, t, angDeg - 90, angDeg + 90, col);
    Arc(e0, rad, t, angDeg + 90, angDeg + 270, col);
}
static void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cr > 0) { Vector2 t = b; b = c; c = t; }
    DrawTriangle(a, b, c, col);
}
static void CubicStroke(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float thick, Color col) {
    Vector2 prev = p0;
    for (int i = 1; i <= 10; i++) {
        float t = i / 10.f, u = 1.f - t;
        Vector2 p = {
            u*u*u*p0.x + 3.f*u*u*t*p1.x + 3.f*u*t*t*p2.x + t*t*t*p3.x,
            u*u*u*p0.y + 3.f*u*u*t*p1.y + 3.f*u*t*t*p2.y + t*t*t*p3.y
        };
        DrawLineEx(prev, p, thick, col);
        prev = p;
    }
    DrawCircleV(p0, thick * .5f, col);
    DrawCircleV(p3, thick * .5f, col);
}
static void TxtAt(int font, const char *s, float x, float y, float size, Color c) {
    int px = PxOf(font, (float)(int)(size + 2.25f));
    float drawSize = (float)px / fmaxf(1.f, gDpiScale);
    DrawTextEx(FontPx(font, px), s, (Vector2){roundf(x), roundf(y)}, drawSize, 0, c);
}
static float TxtW(int font, const char *s, float size) {
    int px = PxOf(font, (float)(int)(size + 2.25f));
    return MeasureTextEx(FontPx(font, px), s, (float)px / fmaxf(1.f, gDpiScale), 0).x;
}

/* ------------------------------------------------------------------ */
/* icons                                                               */
/* ------------------------------------------------------------------ */
static Vector2 IconPoint(float x, float y, float w, float h, float nx, float ny, float rotation) {
    float dx = (nx - .5f) * w, dy = (ny - .5f) * h;
    float a = rotation * DEG2RAD, cs = cosf(a), sn = sinf(a);
    return (Vector2){x + .5f * w + dx * cs - dy * sn, y + .5f * h + dx * sn + dy * cs};
}

/* User-supplied monochrome tool icons; tint at draw time to match the UI theme. */
static Texture2D gToolIcons[6];
static const char *gToolIconFiles[6] = {"color-palette.ico", "link.ico", "palm.ico", "selection.ico", "magnet.ico", "library.ico"};
static Texture2D gKnobTexture, gAppLogo;

static void LoadKnobTexture(void) {
    const int size = 128;
    Image image = GenImageColor(size, size, (Color){0, 0, 0, 0});
    if (!image.data) return;
    Color *pixels = (Color *)image.data;
    const float center = (size - 1) * .5f;
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        float dx = (x - center) / 60.f, dy = (y - center) / 60.f;
        float radius = sqrtf(dx * dx + dy * dy);
        Color color = {0, 0, 0, 0};
        if (radius <= .91f) {
            float t = (float)y / (float)(size - 1);
            color = (Color){(unsigned char)(48.f - 25.f * t), (unsigned char)(49.f - 25.f * t), (unsigned char)(55.f - 27.f * t), 255};
        } else if (radius <= .945f) color = (Color){7, 8, 10, 248};
        else if (radius <= 1.f) color = (Color){88, 90, 98, 255};
        else if (radius <= 1.08f) {
            float alpha = (1.08f - radius) / .08f;
            color = (Color){0, 0, 0, (unsigned char)(36.f * alpha)};
        }
        pixels[y * size + x] = color;
    }
    gKnobTexture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (gKnobTexture.id) SetTextureFilter(gKnobTexture, TEXTURE_FILTER_BILINEAR);
}

static Texture2D LoadToolIconIco(const char *path) {
    Texture2D texture = {0};
#ifdef _WIN32
    const int size = 128;
    HICON icon = (HICON)LoadImageA(NULL, path, IMAGE_ICON, size, size, LR_LOADFROMFILE);
    if (!icon) return texture;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) { DestroyIcon(icon); return texture; }
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *dibPixels = NULL;
    HBITMAP dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &dibPixels, NULL, 0);
    if (!dib || !dibPixels) {
        if (dib) DeleteObject(dib);
        DeleteDC(dc); DestroyIcon(icon); return texture;
    }
    HGDIOBJ previous = SelectObject(dc, dib);
    RECT bounds = {0, 0, size, size};
    FillRect(dc, &bounds, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawIconEx(dc, 0, 0, icon, size, size, 0, NULL, DI_NORMAL);

    Color *rgba = (Color *)malloc((size_t)size * size * sizeof(Color));
    if (rgba) {
        const unsigned char *bgra = (const unsigned char *)dibPixels;
        for (int i = 0; i < size * size; i++) {
            unsigned luminance = (bgra[i * 4 + 2] * 30u + bgra[i * 4 + 1] * 59u + bgra[i * 4] * 11u) / 100u;
            rgba[i] = (Color){255, 255, 255, (unsigned char)(255u - luminance)};
        }
        Image image = {rgba, size, size, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        texture = LoadTextureFromImage(image);
        free(rgba);
    }
    SelectObject(dc, previous);
    DeleteObject(dib);
    DeleteDC(dc);
    DestroyIcon(icon);
#else
    (void)path;
#endif
    return texture;
}

static Texture2D LoadAppLogoIco(const char *path) {
    Texture2D texture = {0};
#ifdef _WIN32
    const int size = 128;
    HICON icon = (HICON)LoadImageA(NULL, path, IMAGE_ICON, size, size, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!icon) return texture;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) { DestroyIcon(icon); return texture; }
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *dibPixels = NULL;
    HBITMAP dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &dibPixels, NULL, 0);
    if (!dib || !dibPixels) {
        if (dib) DeleteObject(dib);
        DeleteDC(dc); DestroyIcon(icon); return texture;
    }
    HGDIOBJ previous = SelectObject(dc, dib);
    memset(dibPixels, 0, (size_t)size * size * 4);
    DrawIconEx(dc, 0, 0, icon, size, size, 0, NULL, DI_NORMAL);
    Color *rgba = (Color *)malloc((size_t)size * size * sizeof(Color));
    if (rgba) {
        const unsigned char *bgra = (const unsigned char *)dibPixels;
        for (int i = 0; i < size * size; i++) {
            unsigned char alpha = bgra[i * 4 + 3];
            if (alpha == 0 && (bgra[i * 4] || bgra[i * 4 + 1] || bgra[i * 4 + 2])) alpha = 255;
            rgba[i] = (Color){bgra[i * 4 + 2], bgra[i * 4 + 1], bgra[i * 4], alpha};
        }
        Image image = {rgba, size, size, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        texture = LoadTextureFromImage(image);
        free(rgba);
    }
    SelectObject(dc, previous);
    DeleteObject(dib);
    DeleteDC(dc);
    DestroyIcon(icon);
#else
    (void)path;
#endif
    return texture;
}

static bool DrawAssetIcon(int k, Rectangle r, Color tint) {
    int slot = k == I_PALETTE ? 0 : k == I_LINK ? 1 : k == I_HAND ? 2 : k == I_POINTER ? 3 :
               k == I_MAGNET ? 4 : k == I_LIBRARY ? 5 : -1;
    if (slot < 0 || gToolIcons[slot].id == 0) return false;
    Texture2D tex = gToolIcons[slot];
    if (k == I_LINK) { r.x += r.width * .19f; r.y += r.height * .19f; r.width *= .62f; r.height *= .62f; }
    DrawTexturePro(tex, (Rectangle){0, 0, (float)tex.width, (float)tex.height}, r, (Vector2){0, 0}, 0.f, tint);
    return true;
}

static void LoadUiIcons(void) {
    const char *appDir = GetApplicationDirectory();
    for (int i = 0; i < 6; i++) {
        const char *path = TextFormat("%sIcons/Tools/%s", appDir, gToolIconFiles[i]);
        if (!FileExists(path)) path = TextFormat("Icons/Tools/%s", gToolIconFiles[i]);
        if (FileExists(path)) {
            gToolIcons[i] = LoadToolIconIco(path);
            if (gToolIcons[i].id) SetTextureFilter(gToolIcons[i], TEXTURE_FILTER_BILINEAR);
        }
    }
    const char *logoPath = TextFormat("%sIcons/Logo/Stella.ico", appDir);
    if (!FileExists(logoPath)) logoPath = "Icons/Logo/Stella.ico";
    if (FileExists(logoPath)) {
        gAppLogo = LoadAppLogoIco(logoPath);
        if (gAppLogo.id) SetTextureFilter(gAppLogo, TEXTURE_FILTER_BILINEAR);
    }
}

static void UnloadUiIcons(void) {
    for (int i = 0; i < 6; i++) if (gToolIcons[i].id) UnloadTexture(gToolIcons[i]);
    if (gKnobTexture.id) UnloadTexture(gKnobTexture);
    if (gAppLogo.id) UnloadTexture(gAppLogo);
}

static void DrawIcon(int k, Rectangle r, Color c, float v) {
    float x = r.x, y = r.y, w = r.width, h = r.height;
    float t = fmaxf(1.3f, w / 11.0f);
#define P(a, b) IconPoint(x, y, w, h, a, b, (k == I_RESET || k == I_ROT) ? v : 0.f)
#define LN(a, b, c2, d) DrawLineEx(P(a, b), P(c2, d), t, c)
    switch (k) {
    case I_PLUS:    LN(.22, .5, .78, .5); if (v > 0.01f) LN(.5, .5 - .28 * v, .5, .5 + .28 * v); break;
    case I_CHEV_R:  LN(.38, .2, .66, .5); LN(.66, .5, .38, .8); break;
    case I_CHEV_L:  LN(.62, .2, .34, .5); LN(.34, .5, .62, .8); break;
    case I_CHEV_UP: LN(.25, .62, .5, .38); LN(.5, .38, .75, .62); break;
    case I_CHEV_DN: LN(.25, .38, .5, .62); LN(.5, .62, .75, .38); break;
    case I_SEARCH:  Ring(P(.44, .44), .28 * w, t, c); LN(.65, .65, .88, .88); break;
    case I_LIBRARY: StrokeRR((Rectangle){x + .1f * w, y + .15f * h, .8f * w, .7f * h}, .12f * w, t, c);
                    LN(.31, .15, .31, .85); LN(.31, .38, .87, .38); LN(.31, .62, .87, .62); break;
    case I_UNDO:    Arc(P(.5, .58), .3f * w, t, 215, 450, c); LN(.254, .408, .254, .2); LN(.254, .408, .47, .408); break;
    case I_REDO:    Arc(P(.5, .58), .3f * w, t, 90, 325, c);  LN(.746, .408, .746, .2); LN(.746, .408, .53, .408); break;
    case I_EXPORT:  LN(.5, .62, .5, .14); LN(.5, .14, .32, .32); LN(.5, .14, .68, .32);
                    LN(.2, .6, .2, .86); LN(.2, .86, .8, .86); LN(.8, .86, .8, .6); break;
    case I_NOTE:    DrawCircleV(P(.3, .76), .13f * w, c); DrawCircleV(P(.72, .66), .13f * w, c);
                    LN(.4, .76, .4, .2); LN(.82, .66, .82, .1); LN(.4, .2, .82, .1); break;
    case I_PALETTE: Ring(P(.5, .5), .4f * w, t, c);
                    DrawCircleV(P(.34, .4), .06f * w, c); DrawCircleV(P(.52, .3), .06f * w, c);
                    DrawCircleV(P(.7, .42), .06f * w, c); DrawCircleV(P(.38, .65), .06f * w, c);
                    Ring(P(.66, .72), .1f * w, t * .8f, c); break;
    case I_GRADING: Ring(P(.5, .5), .4f * w, t, c);
                    DrawCircleV(P(.5, .32), .08f * w, c); DrawCircleV(P(.33, .62), .08f * w, c); DrawCircleV(P(.67, .62), .08f * w, c); break;
    case I_SLIDERS: LN(.1, .3, .9, .3); LN(.1, .7, .9, .7);
                    DrawCircleV(P(.65, .3), .11f * w, c); DrawCircleV(P(.35, .7), .11f * w, c); break;
    case I_SUN:     Ring(P(.5, .5), .2f * w, t, c);
                    for (int i = 0; i < 8; i++) { float a = i * PI / 4; float s = sinf(a), co = cosf(a);
                        DrawLineEx((Vector2){x + (.5f + .32f * co) * w, y + (.5f + .32f * s) * h},
                                   (Vector2){x + (.5f + .45f * co) * w, y + (.5f + .45f * s) * h}, t, c); } break;
    case I_DROP:    Arc(P(.5, .64), .30f * w, t, 327.6f, 572.4f, c); LN(.5, .08, .247, .479); LN(.5, .08, .753, .479); break;
    case I_MOUNTAIN:LN(.08, .8, .4, .28); LN(.4, .28, .7, .8); LN(.08, .8, .92, .8); LN(.6, .55, .72, .4); LN(.72, .4, .92, .8); break;
    case I_CROP:    LN(.3, .1, .3, .7); LN(.3, .7, .9, .7); LN(.1, .3, .7, .3); LN(.7, .3, .7, .9); break;
    case I_POS:     LN(.5, .08, .5, .36); LN(.5, .64, .5, .92); LN(.08, .5, .36, .5); LN(.64, .5, .92, .5); DrawCircleV(P(.5, .5), .05f * w, c); break;
    case I_SCALE:   LN(.12, .88, .88, .12); LN(.55, .12, .88, .12); LN(.88, .12, .88, .45); LN(.12, .55, .12, .88); LN(.12, .88, .45, .88); break;
    case I_RESET:   Arc(P(.5, .5), .34f * w, t, 210, 500, c); LN(.2, .33, .2, .1); LN(.2, .33, .42, .33); break;
    case I_ROT:     Arc(P(.5, .5), .34f * w, t, 210, 500, c); LN(.2, .33, .2, .1); LN(.2, .33, .42, .33); break;
    case I_LINK: {
                    /* Two open, interlocking chain links, drawn with a clean rounded stroke. */
                    CubicStroke(P(.43, .47), P(.50, .37), P(.58, .27), P(.65, .22), t, c);
                    CubicStroke(P(.65, .22), P(.78, .12), P(.90, .23), P(.85, .39), t, c);
                    CubicStroke(P(.85, .39), P(.81, .48), P(.72, .56), P(.65, .63), t, c);
                    CubicStroke(P(.35, .37), P(.27, .45), P(.19, .53), P(.15, .62), t, c);
                    CubicStroke(P(.15, .62), P(.10, .79), P(.24, .90), P(.39, .80), t, c);
                    CubicStroke(P(.39, .80), P(.47, .73), P(.54, .64), P(.57, .56), t, c);
                } break;
    case I_DISTORT: StrokeRR((Rectangle){x + .1f * w, y + .15f * h, .8f * w, .7f * h}, .14f * w, t, c);
                    LN(.1, .5, .9, .5); LN(.5, .15, .5, .5); break;
    case I_SKIP_B:  LN(.28, .2, .28, .8); LN(.76, .2, .4, .5); LN(.4, .5, .76, .8); break;
    case I_SKIP_F:  LN(.72, .2, .72, .8); LN(.24, .2, .6, .5); LN(.6, .5, .24, .8); break;
    case I_PLAY:    Tri(P(.3, .16), P(.3, .84), P(.84, .5), c); break;
    case I_PAUSE:  FillRR((Rectangle){x + .27f * w, y + .2f * h, .16f * w, .6f * h}, 1.f, c);
                    FillRR((Rectangle){x + .58f * w, y + .2f * h, .16f * w, .6f * h}, 1.f, c); break;
    case I_LOOP:    LN(.25, .35, .75, .35); LN(.75, .35, .75, .48); LN(.75, .65, .25, .65); LN(.25, .65, .25, .52);
                    LN(.66, .24, .78, .35); LN(.78, .35, .66, .46); LN(.34, .54, .22, .65); LN(.22, .65, .34, .76); break;
    case I_GRID:    StrokeRR((Rectangle){x + .14f * w, y + .14f * h, .72f * w, .72f * h}, .08f * w, t, c);
                    LN(.38, .14, .38, .86); LN(.62, .14, .62, .86); LN(.14, .38, .86, .38); LN(.14, .62, .86, .62); break;
    case I_POINTER: LN(.28, .1, .28, .8); LN(.28, .8, .45, .64); LN(.45, .64, .7, .64); LN(.7, .64, .28, .1); break;
    case I_HAND:    StrokeRR((Rectangle){x + .28f * w, y + .42f * h, .44f * w, .42f * h}, .1f * w, t, c);
                    LN(.34, .42, .34, .16); LN(.45, .42, .45, .1); LN(.56, .42, .56, .16); LN(.66, .42, .66, .26); break;
    case I_SCISSORS:Ring(P(.27, .74), .13f * w, t, c); Ring(P(.73, .74), .13f * w, t, c); LN(.33, .64, .8, .12); LN(.67, .64, .2, .12); break;
    case I_TEXT:    LN(.2, .2, .8, .2); LN(.5, .2, .5, .84); LN(.2, .2, .2, .3); LN(.8, .2, .8, .3); LN(.38, .84, .62, .84); break;
    case I_TRASH:   LN(.15, .25, .85, .25); LN(.4, .25, .4, .14); LN(.4, .14, .6, .14); LN(.6, .14, .6, .25);
                    LN(.25, .25, .3, .88); LN(.3, .88, .7, .88); LN(.7, .88, .75, .25); LN(.43, .4, .43, .75); LN(.57, .4, .57, .75); break;
    case I_KF_PREV: LN(.3, .2, .3, .8); LN(.72, .28, .46, .5); LN(.46, .5, .72, .72); break;
    case I_KF_NEXT: LN(.7, .2, .7, .8); LN(.28, .28, .54, .5); LN(.54, .5, .28, .72); break;
    case I_KF_DIAMOND: LN(.5, .1, .88, .5); LN(.88, .5, .5, .9); LN(.5, .9, .12, .5); LN(.12, .5, .5, .1);
                    LN(.5, .32, .68, .5); LN(.68, .5, .5, .68); LN(.5, .68, .32, .5); LN(.32, .5, .5, .32); break;
    case I_ZOOM_OUT:Ring(P(.44, .44), .3f * w, t, c); LN(.66, .66, .9, .9); LN(.3, .44, .58, .44); break;
    case I_ZOOM_IN: Ring(P(.44, .44), .3f * w, t, c); LN(.66, .66, .9, .9); LN(.3, .44, .58, .44); LN(.44, .3, .44, .58); break;
    case I_MAGNET:  {
                    float mt = fmaxf(2.f, t * 1.35f);
                    CubicStroke(P(.22, .25), P(.22, .58), P(.36, .79), P(.5, .79), mt, c);
                    CubicStroke(P(.5, .79), P(.64, .79), P(.78, .58), P(.78, .25), mt, c);
                    LN(.22, .28, .39, .28); LN(.61, .28, .78, .28);
                } break;
    case I_FILM:    StrokeRR((Rectangle){x + .1f * w, y + .18f * h, .8f * w, .64f * h}, .1f * w, t, c);
                    LN(.34, .18, .34, .82); LN(.66, .18, .66, .82); break;
    case I_BOWTIE:  LN(.1, .22, .1, .78); LN(.1, .22, .5, .5); LN(.1, .78, .5, .5);
                    LN(.9, .22, .9, .78); LN(.9, .22, .5, .5); LN(.9, .78, .5, .5); break;
    case I_SPARKLE: LN(.5, .08, .58, .42); LN(.58, .42, .92, .5); LN(.92, .5, .58, .58); LN(.58, .58, .5, .92);
                    LN(.5, .92, .42, .58); LN(.42, .58, .08, .5); LN(.08, .5, .42, .42); LN(.42, .42, .5, .08); break;
    case I_MINIMIZE: LN(.18, .5, .82, .5); break;
    case I_MAXIMIZE: StrokeRR((Rectangle){x + .2f * w, y + .2f * h, .6f * w, .6f * h}, 1.5f, t, c); break;
    case I_CLOSE: LN(.22, .22, .78, .78); LN(.78, .22, .22, .78); break;
    default: break;
    }
#undef P
#undef LN
}


/* ------------------------------------------------------------------ */
/* custom elements                                                     */
/* ------------------------------------------------------------------ */
static void DrawCustom(Cust *cu, Rectangle r) {
    if (cu->kind == K_APP_LOGO) {
        if (gAppLogo.id) DrawTexturePro(gAppLogo, (Rectangle){0, 0, (float)gAppLogo.width, (float)gAppLogo.height},
                                        r, (Vector2){0, 0}, 0.f, WHITE);
        else {
            FillRR(r, 7.f, (Color){20, 21, 25, 255});
            StrokeRR(r, 7.f, 1.f, (Color){64, 66, 73, 255});
        }
        return;
    }
    if (cu->kind < K_THUMB) {
        if (!DrawAssetIcon(cu->kind, r, RC(cu->a))) DrawIcon(cu->kind, r, RC(cu->a), cu->v);
        return;
    }
    switch (cu->kind) {
    case K_THUMB: {
        int idx = (cu->i >> 4) & 255;
        bool imported = (cu->i & 0x1000) != 0;
        const char *key = imported && idx < gImportedCount ? gImported[idx].path : NULL;
        Color swatch = MediaSwatch(key, idx);
        Texture2D thumbnail = imported && idx < gImportedCount ? gImported[idx].thumbnail : (Texture2D){0};
        if (thumbnail.id) {
            DrawTexturePro(thumbnail, (Rectangle){0, 0, (float)thumbnail.width, (float)thumbnail.height}, r,
                           (Vector2){0, 0}, 0.f, WHITE);
            FillRR(r, 10, (Color){10, 12, 16, 25});
        } else GradRR(r, 10, ShadeMediaColor(swatch, .18f), ShadeMediaColor(swatch, -.22f));
        FillRR(r, 10, (Color){255, 255, 255, U8(20 * cu->f)});
        StrokeRR(r, 10, 1, (Color){255, 255, 255, U8(20 + 60 * cu->f)});
        if (cu->f > .03f) {
            Rectangle pb = {r.x + r.width - 30, r.y + 6, 24, 24};
            FillRR(pb, 7, (Color){246, 246, 249, U8(255 * fminf(1.f, cu->f * 1.4f))});
            Color d = (Color){30, 31, 36, U8(255 * fminf(1.f, cu->f * 1.4f))};
            DrawLineEx((Vector2){pb.x + 7, pb.y + 12}, (Vector2){pb.x + 17, pb.y + 12}, 2.f, d);
            DrawLineEx((Vector2){pb.x + 12, pb.y + 7}, (Vector2){pb.x + 12, pb.y + 17}, 2.f, d);
        }
    } break;

    case K_KNOB: {
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        float scale = cu->b.r;
        float rad = (fminf(r.width, r.height) / 2 - 1) * scale;
        float hv = cu->f;
        float drawSize = fminf(r.width, r.height) * scale;
        Rectangle dst = {c.x - drawSize * .5f, c.y - drawSize * .5f, drawSize, drawSize};
        if (gKnobTexture.id)
            DrawTexturePro(gKnobTexture, (Rectangle){0, 0, (float)gKnobTexture.width, (float)gKnobTexture.height}, dst,
                           (Vector2){0, 0}, 0.f, WHITE);
        else {
            DrawCircleV(c, rad, (Color){31, 32, 36, 255});
            Ring(c, rad - .5f, 1.2f, (Color){88, 90, 98, 255});
        }
        float ang = (cu->v - .5f) * 4.7f;
        Vector2 d = {sinf(ang), -cosf(ang)};
        Vector2 p0 = {c.x + d.x * rad * .66f, c.y + d.y * rad * .66f};
        Vector2 p1 = {c.x + d.x * rad * .84f, c.y + d.y * rad * .84f};
        DrawLineEx((Vector2){p0.x + 1.f, p0.y + 1.f}, (Vector2){p1.x + 1.f, p1.y + 1.f}, 2.6f, (Color){0, 0, 0, 125});
        DrawLineEx(p0, p1, 1.7f, (Color){235, 237, 242, 255});
    } break;

    case K_WHEEL: {
        /* b.r / b.g = puck position (unit disk), c = puck colour, v = coloured? */
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        float rad = fminf(r.width, r.height) / 2 - 1;
        for (int i = 0; i < 72; i++)
            DrawCircleSector(c, rad, i * 5.f, i * 5.f + 5.6f, 3, ColorFromHSV(fmodf(i * 5.f + 90.f, 360.f), .70f, .62f));
        /* darken + desaturate towards the centre (each ring has its own alpha) */
        for (float rr = rad; rr > 1.f; rr -= 1.f) {
            float t = 1.f - rr / rad;
            DrawRing(c, rr - 1.f, rr, 0, 360, 40, (Color){34, 32, 40, U8(225 * powf(t, 1.35f))});
        }
        Ring(c, rad, 2, (Color){38, 39, 45, 255});
        Vector2 p = {c.x + cu->b.r * rad, c.y + cu->b.g * rad};
        float dot = 9.5f + 1.5f * cu->f;
        DrawCircleV(p, dot + 4.f, (Color){10, 10, 14, 105});
        if (cu->v > .5f) {
            DrawCircleV(p, dot + 2.2f, (Color){235, 236, 240, 255});
            DrawCircleV(p, dot, RC(cu->c));
        }
    } break;

    case K_SWITCH: {
        float rad = r.height / 2;
        FillRR(r, rad, RC(Mix(RGB(44, 45, 51), C_ACCENT, cu->v)));
        StrokeRR(r, rad, 1, (Color){56, 58, 65, 255});
        float kx = r.x + rad + (r.width - 2 * rad) * cu->v;
        DrawCircleV((Vector2){kx, r.y + rad}, rad - 3.f + cu->f, (Color){226, 227, 231, 255});
    } break;

    case K_SLIDER: {
        /* i bit0 = big, bit1 = gradient (a -> b), bit2 = rainbow,
           bit3 = progress fill from the CENTRE (bipolar), bit4 = 3-stop gradient (a -> c -> b).
           No gradient bits: dark track (a) + progress fill (b) from the left. */
        bool big = cu->i & 1, grad = cu->i & 2, rainbow = cu->i & 4, bip = cu->i & 8, tri = cu->i & 16;
        float th = big ? 6.f : 4.f, kr = big ? 8.5f : 7.f;
        float cy = r.y + r.height / 2;
        Rectangle tr = {r.x, cy - th / 2, r.width, th};
        Rectangle gr = {tr.x + th / 2, tr.y, tr.width - th, th};      /* gradient body, caps are round */
        if (rainbow) {
            Color cs[7] = {{240,60,60,255},{240,220,60,255},{60,220,90,255},{60,220,230,255},{70,100,250,255},{230,70,230,255},{240,60,60,255}};
            float sw = gr.width / 6.f;
            for (int i = 0; i < 6; i++)
                DrawRectangleGradientH((int)(gr.x + sw * i), (int)gr.y, (int)ceilf(sw), (int)th, cs[i], cs[i + 1]);
            DrawCircleV((Vector2){gr.x, cy}, th / 2, cs[0]);
            DrawCircleV((Vector2){gr.x + gr.width, cy}, th / 2, cs[6]);
        } else if (grad) {
            Color ca = RC(cu->a), cb = RC(cu->b), cm = RC(cu->c);
            if (tri) {
                int hw = (int)(gr.width / 2);
                DrawRectangleGradientH((int)gr.x, (int)gr.y, hw, (int)th, ca, cm);
                DrawRectangleGradientH((int)gr.x + hw, (int)gr.y, (int)(gr.width - hw), (int)th, cm, cb);
            } else {
                DrawRectangleGradientH((int)gr.x, (int)gr.y, (int)gr.width, (int)th, ca, cb);
            }
            DrawCircleV((Vector2){gr.x, cy}, th / 2, ca);
            DrawCircleV((Vector2){gr.x + gr.width, cy}, th / 2, cb);
        } else {
            FillRR(tr, th / 2, RC(cu->a));
            float kx = kr + (r.width - 2 * kr) * cu->v;
            if (bip) {
                float cx = r.width / 2.f, x0 = fminf(cx, kx), x1 = fmaxf(cx, kx);
                if (x1 - x0 > .5f) FillRR((Rectangle){r.x + x0, cy - th / 2, x1 - x0, th}, th / 2, RC(cu->b));
            } else {
                FillRR((Rectangle){r.x, cy - th / 2, kx, th}, th / 2, RC(cu->b));
            }
        }
        Vector2 kc = {r.x + kr + (r.width - 2 * kr) * cu->v, cy};
        DrawCircleV((Vector2){kc.x, kc.y + 1.f}, kr + cu->f + 1.f, (Color){0, 0, 0, 70});
        DrawCircleV(kc, kr + cu->f, (Color){244, 244, 246, 255});
    } break;

    case K_ROTRULER: {
        /* v = normalised marker position 0..1; box is 322 x 49, labels + pointer sit underneath */
        Rectangle box = {r.x, r.y, r.width, 49};
        GradRR(box, 14, (Color){11, 12, 14, 255}, (Color){15, 16, 18, 255});
        float x0 = box.x + RULER_L, x1 = box.x + box.width - RULER_R, cy = box.y + 24.5f;
        for (int i = 0; i <= 36; i++) {
            float px = x0 + (x1 - x0) * i / 36.f;
            if (i % 9 == 0)       DrawLineEx((Vector2){px, cy - 13.5f}, (Vector2){px, cy + 13.5f}, 1.7f, (Color){237, 237, 240, 255});
            else if (i % 3 == 0)  DrawLineEx((Vector2){px, cy - 7.5f},  (Vector2){px, cy + 7.5f},  1.7f, (Color){122, 123, 128, 255});
            else                  DrawLineEx((Vector2){px, cy - 5.f},   (Vector2){px, cy + 5.f},   1.5f, (Color){99, 100, 104, 255});
        }
        const char *lab[5] = {"-180\xC2\xB0", "-90\xC2\xB0", "0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0"};
        for (int i = 0; i < 5; i++) {
            float px = x0 + (x1 - x0) * (i * 9) / 36.f;
            TxtAt(0, lab[i], px - TxtW(0, lab[i], 17) / 2, box.y + 57, 17, RC(C_ICON));
        }
        float mx = x0 + (x1 - x0) * cu->v, my = cy + .5f;
        /* soft red glow: stacked translucent capsules */
        for (int k = 7; k >= 1; k--)
            FillRR((Rectangle){mx - 2 - k, my - 19 - k, 4 + 2 * k, 38 + 2 * k}, 2 + k, (Color){255, 59, 59, 7});
        FillRR((Rectangle){mx - 2, my - 19, 4, 38}, 2, (Color){255, 59, 59, 255});
        Tri((Vector2){mx, r.y + 79}, (Vector2){mx - 7, r.y + 89}, (Vector2){mx + 7, r.y + 89}, (Color){244, 244, 246, 255});
    } break;

    case K_RULER: {
        const float pps = TimelinePps();
        float bottom = r.y + r.height;
        int startSec = (int)floorf(-gTimelinePan / pps);
        if (startSec < 0) startSec = 0;
        for (int s = startSec;; s++) {
            float px = r.x + gTimelinePan + s * pps;
            if (px > r.x + r.width) break;
            if (px < r.x) continue;
            bool major = (s % 5 == 0);
            DrawLineEx((Vector2){px, bottom - (major ? 10 : 5)}, (Vector2){px, bottom}, 1.f, (Color){72, 75, 84, 255});
            if (major) { char b[16]; snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60); TxtAt(0, b, px + 3, r.y + 3, 12, RC(C_MUTED)); }
        }
        gPHPos = (Vector2){r.x + gTimelinePan + gPlayhead * pps, r.y + 4};
        gPH = gPHPos.x >= r.x && gPHPos.x <= r.x + r.width;
        gPHLen = r.height - 4 + cu->f;
    } break;

    case K_PREVIEW: {
        DrawRectangleRec(r, BLACK);
        if (gPreviewFrame.id) {
            DrawTexturePro(gPreviewFrame, (Rectangle){0, 0, (float)gPreviewFrame.width, (float)gPreviewFrame.height},
                           r, (Vector2){0, 0}, 0.f, WHITE);
        }
        if (gTimelineGrid) {
            Color grid = (Color){210, 214, 224, 70};
            DrawLineEx((Vector2){r.x + r.width / 3.f, r.y}, (Vector2){r.x + r.width / 3.f, r.y + r.height}, 1.f, grid);
            DrawLineEx((Vector2){r.x + 2.f * r.width / 3.f, r.y}, (Vector2){r.x + 2.f * r.width / 3.f, r.y + r.height}, 1.f, grid);
            DrawLineEx((Vector2){r.x, r.y + r.height / 3.f}, (Vector2){r.x + r.width, r.y + r.height / 3.f}, 1.f, grid);
            DrawLineEx((Vector2){r.x, r.y + 2.f * r.height / 3.f}, (Vector2){r.x + r.width, r.y + 2.f * r.height / 3.f}, 1.f, grid);
        }
        DrawRectangleLinesEx(r, 1.5f, WHITE);
        Vector2 q[4] = {{r.x, r.y}, {r.x + r.width, r.y}, {r.x, r.y + r.height}, {r.x + r.width, r.y + r.height}};
        for (int i = 0; i < 4; i++) { DrawCircleV(q[i], 5.5f, WHITE); }
    } break;

    case K_CLIP: {
        int clipIndex = cu->i >> 1;
        TimelineClipUi *clip = clipIndex >= 0 && clipIndex < gTimelineClipCount ? &gTimelineClips[clipIndex] : NULL;
        const char *mediaKey = clip ? clip->path : NULL;
        Color swatch = MediaSwatch(mediaKey, clipIndex);
        GradRR(r, 6, ShadeMediaColor(swatch, .18f), ShadeMediaColor(swatch, -.22f));
        Texture2D wave = {0};
        if (clip && clip->mediaIndex >= 0 && clip->mediaIndex < gImportedCount) {
            ImportedMedia *media = &gImported[clip->mediaIndex];
            wave = media->waveform;
        }
        bool hasAudio = clip && (clip->track >= 4 || clip->hasAudio);
        if (wave.id && hasAudio) {
            Rectangle band = {r.x + 6, r.y + r.height - 23, fmaxf(0.f, r.width - 12), 19};
            FillRR(band, 3.f, (Color){54, 112, 150, 175});
            DrawLineEx((Vector2){r.x + 6, band.y - 1}, (Vector2){r.x + r.width - 6, band.y - 1},
                       1.f, (Color){125, 171, 196, 210});
            Rectangle waveformRect = {r.x + 8, band.y, fmaxf(0.f, r.width - 16), band.height};
            DrawTexturePro(wave, (Rectangle){0, 0, (float)wave.width, (float)wave.height}, waveformRect,
                           (Vector2){0, 0}, 0.f, (Color){81, 134, 166, 255});
        }
        FillRR(r, 6, (Color){255, 255, 255, U8(18 * cu->f)});
        bool selected = (cu->i & 1) != 0;
        StrokeRR(r, 6, selected ? 2.f : 1.f, selected ? (Color){245, 245, 250, 255} : (Color){113, 116, 124, 255});
        if (clipIndex >= 0 && clipIndex < gTimelineClipCount) {
            TimelineClipUi *clip = &gTimelineClips[clipIndex];
            for (int k = 0; k < clip->keyframeCount; k++) {
                float time = clip->keyframes[k];
                if (time < clip->start || time > clip->start + clip->duration) continue;
                float x = r.x + (time - clip->start) / fmaxf(.001f, clip->duration) * r.width;
                float y = r.y + 13.f, half = 5.5f;
                Color key = fabsf(time - gPlayhead) < (1.f / 60.f) ? (Color){255, 255, 255, 255} : (Color){226, 219, 247, 245};
                Vector2 a = {x, y - half}, b = {x + half, y}, c = {x, y + half}, d = {x - half, y};
                DrawTriangle(a, d, b, key); DrawTriangle(c, b, d, key);
                DrawLineEx(a, b, 1.f, (Color){72, 58, 90, 255});
                DrawLineEx(b, c, 1.f, (Color){72, 58, 90, 255});
                DrawLineEx(c, d, 1.f, (Color){72, 58, 90, 255});
                DrawLineEx(d, a, 1.f, (Color){72, 58, 90, 255});
            }
        }
    } break;

    case K_SCROLLBAR: {
        /* v = scroll position 0..1, f = visible fraction (>=1 means nothing to scroll) */
        if (cu->f >= .999f) break;
        float th = fmaxf(30.f, r.height * cu->f);
        float ty = r.y + (r.height - th) * cu->v;
        FillRR((Rectangle){r.x, ty, r.width, th}, r.width / 2, (Color){84, 87, 96, 255});
    } break;

    case K_PILL: {
        int n = cu->i > 0 ? cu->i : 4;
        FillRR(r, 14, (Color){12, 13, 15, 255});
        if (n == 2) StrokeRR(r, 14, 1, (Color){30, 32, 37, 255});
        float tw = (r.width - 8.f - 4.f * (n - 1)) / (float)n;
        Rectangle p = {r.x + 4.f + cu->v * (tw + 4.f), r.y + 4.f, tw, r.height - 8.f};
        FillRR(p, 11, (Color){42, 43, 49, 255});
        StrokeRR(p, 11, 1, (Color){55, 56, 62, 255});
    } break;

    case K_FILTER: {
        /* a,b gradient, c sun colour, f hover, i selected */
        GradRR(r, 10, RC(cu->a), RC(cu->b));
        DrawCircleV((Vector2){r.x + r.width * .72f, r.y + r.height * .26f}, r.width * .12f, RC(cu->c));
        FillRR(r, 10, (Color){255, 255, 255, U8(22 * cu->f)});
        if (cu->i) {
            StrokeRR((Rectangle){r.x - 1, r.y - 1, r.width + 2, r.height + 2}, 11, 2, (Color){240, 241, 245, 255});
        }
    } break;

    case K_CHIP: {
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        float rad = r.width / 2;
        if (cu->i) {
            for (int i = 0; i < 36; i++)
                DrawCircleSector(c, rad, i * 10.f, i * 10.f + 11.f, 3, ColorFromHSV(fmodf(i * 10.f + 90.f, 360.f), .72f, .92f));
            DrawCircleV(c, rad * .42f, (Color){240, 240, 244, 30});
        } else {
            DrawCircleV(c, rad, RC(cu->a));
        }
    } break;

    case K_SPLIT: {
        /* v = activity 0..1 (hover/drag), i = 1 for a vertical bar (resizes width) */
        if (cu->v < .01f) break;
        Color ac = (Color){64, 120, 255, U8(230 * cu->v)};
        if (cu->i) FillRR((Rectangle){r.x + r.width / 2 - 1.f, r.y, 2.f, r.height}, 1.f, ac);
        else       FillRR((Rectangle){r.x, r.y + r.height / 2 - 1.f, r.width, 2.f}, 1.f, ac);
    } break;

    case K_GLOW: {
        /* link toggle backdrop. v = linked amount 0..1 (soft blue glow), f = white hover/press alpha */
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        if (cu->f > .5f) DrawCircleV(c, r.width / 2, (Color){255, 255, 255, U8(cu->f * (1.f - cu->v))});
        if (cu->v > .01f) {
            Color g = RC(cu->a);
            float a = 36.f * cu->v;
            DrawCircleV(c, 14.f, (Color){g.r, g.g, g.b, U8(a)});
            for (int i = 0; i < 10; i++)
                DrawRing(c, 14.f + i, 15.f + i, 0, 360, 48, (Color){g.r, g.g, g.b, U8(a * (1.f - (i + .5f) / 10.f))});
        }
    } break;

    case K_ROT_ICON:
        DrawIcon(cu->i, r, RC(cu->a), cu->v);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Clay -> raylib renderer (with nested scissor stack)                 */
/* ------------------------------------------------------------------ */
static Rectangle gScis[16];
static int gScisN;
static void ApplyScissor(void) {
    if (gScisN > 0) {
        Rectangle s = gScis[gScisN - 1];
        BeginScissorMode((int)s.x, (int)s.y, (int)s.width, (int)s.height);
    }
}

static void RenderUI(Clay_RenderCommandArray cmds) {
    gPH = false;
    gScisN = 0;
    for (int i = 0; i < cmds.length; i++) {
        Clay_RenderCommand *rc = Clay_RenderCommandArray_Get(&cmds, i);
        Clay_BoundingBox bb = rc->boundingBox;
        Rectangle r = {bb.x, bb.y, bb.width, bb.height};
        switch (rc->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            Clay_RectangleRenderData *d = &rc->renderData.rectangle;
            Clay_Color bc = d->backgroundColor;
            if (bc.r == 27 && bc.g == 28 && bc.b == 31 && bc.a == 255)
                GradRR(r, d->cornerRadius.topLeft, (Color){28, 29, 32, 255}, (Color){21, 22, 24, 255});
            else FillRR(r, d->cornerRadius.topLeft, RC(bc));
        } break;
        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            Clay_BorderRenderData *d = &rc->renderData.border;
            Color c = RC(d->color);
            float rad = d->cornerRadius.topLeft;
            if (d->width.left && d->width.left == d->width.right && d->width.left == d->width.top && d->width.left == d->width.bottom) {
                StrokeRR(r, rad, d->width.left, c);
            } else {
                if (d->width.top)    DrawRectangleRec((Rectangle){r.x, r.y, r.width, d->width.top}, c);
                if (d->width.bottom) DrawRectangleRec((Rectangle){r.x, r.y + r.height - d->width.bottom, r.width, d->width.bottom}, c);
                if (d->width.left)   DrawRectangleRec((Rectangle){r.x, r.y, d->width.left, r.height}, c);
                if (d->width.right)  DrawRectangleRec((Rectangle){r.x + r.width - d->width.right, r.y, d->width.right, r.height}, c);
            }
        } break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            Clay_TextRenderData *d = &rc->renderData.text;
            char buf[512]; int n = d->stringContents.length < 511 ? d->stringContents.length : 511;
            memcpy(buf, d->stringContents.chars, n); buf[n] = 0;
            int px = PxOf(d->fontId, (float)d->fontSize);
            DrawTextEx(FontPx(d->fontId, px), buf, (Vector2){roundf(r.x), roundf(r.y)}, (float)px / fmaxf(1.f, gDpiScale), d->letterSpacing, RC(d->textColor));
        } break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
            float x0 = roundf(r.x), y0 = roundf(r.y), x1 = roundf(r.x + r.width), y1 = roundf(r.y + r.height);
            if (gScisN > 0) {
                Rectangle p = gScis[gScisN - 1];
                x0 = fmaxf(x0, p.x); y0 = fmaxf(y0, p.y);
                x1 = fminf(x1, p.x + p.width); y1 = fminf(y1, p.y + p.height);
            }
            Rectangle n = {x0, y0, fmaxf(0.f, x1 - x0), fmaxf(0.f, y1 - y0)};
            if (gScisN < 16) gScis[gScisN++] = n;
            ApplyScissor();
        } break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            if (gScisN > 0) gScisN--;
            EndScissorMode();
            ApplyScissor();
            break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
            Cust *cu = (Cust *)rc->renderData.custom.customData;
            if (cu) DrawCustom(cu, r);
        } break;
        default: break;
        }
    }
    if (gScisN > 0) { gScisN = 0; EndScissorMode(); }
    if (gMediaDropPreviewVisible && gMediaDropPreview.width > 0.f) {
        DrawDottedRect(gMediaDropPreview, 4.f, 4.f, 2.f, (Color){190, 150, 232, 235});
    }
    if (gPH) {
        Color red = {240, 62, 72, 255};
        DrawLineEx(gPHPos, (Vector2){gPHPos.x, gPHPos.y + gPHLen}, 1.6f, red);
        DrawRectangleRec((Rectangle){gPHPos.x - 5, gPHPos.y - 4, 10, 8}, red);
        Tri((Vector2){gPHPos.x - 5, gPHPos.y + 4}, (Vector2){gPHPos.x + 5, gPHPos.y + 4}, (Vector2){gPHPos.x, gPHPos.y + 10}, red);
    }
    for (int i = 0; i < gTransitionButtonCount; i++) {
        TransitionButton *button = &gTransitionButtons[i];
        Rectangle r = button->rect;
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        if (button->applied) {
            Rectangle span = button->durationRect;
            if (span.width > 1.f) {
                FillRR(span, 2.5f, (Color){70, 157, 224, 68});
                DrawLineEx((Vector2){span.x, span.y + .5f},
                           (Vector2){span.x + span.width, span.y + .5f}, 1.4f,
                           (Color){111, 191, 248, 220});
                DrawLineEx((Vector2){span.x, span.y - 1.f},
                           (Vector2){span.x, span.y + span.height + 1.f}, 1.2f,
                           (Color){111, 191, 248, 230});
                DrawLineEx((Vector2){span.x + span.width, span.y - 1.f},
                           (Vector2){span.x + span.width, span.y + span.height + 1.f}, 1.2f,
                           (Color){111, 191, 248, 230});
            }
            DrawCircleV((Vector2){r.x + r.width * .5f, r.y + r.height * .5f}, r.width * .5f,
                        (Color){245, 246, 249, 255});
            if (hover) DrawCircleLines((int)(r.x + r.width * .5f), (int)(r.y + r.height * .5f),
                                       r.width * .5f, (Color){112, 190, 245, 255});
            DrawIcon(I_BOWTIE, (Rectangle){r.x + 5.f, r.y + 5.f, r.width - 10.f, r.height - 10.f},
                     (Color){35, 37, 42, 255}, 0.f);
        } else if (hover) {
            DrawCircleV((Vector2){r.x + r.width * .5f, r.y + r.height * .5f}, r.width * .5f,
                        (Color){43, 45, 51, 255});
            DrawCircleLines((int)(r.x + r.width * .5f), (int)(r.y + r.height * .5f), r.width * .5f,
                            (Color){92, 95, 104, 255});
            DrawLineEx((Vector2){r.x + 6.f, r.y + 12.f}, (Vector2){r.x + 18.f, r.y + 12.f}, 1.7f,
                       (Color){237, 238, 241, 255});
            DrawLineEx((Vector2){r.x + 12.f, r.y + 6.f}, (Vector2){r.x + 12.f, r.y + 18.f}, 1.7f,
                       (Color){237, 238, 241, 255});
        }
    }
    if (gFadeA > 2.f && gFadeR.width > 0.f)
        GradRR(gFadeR, 22, (Color){28, 29, 32, U8(gFadeA)}, (Color){21, 22, 24, U8(gFadeA)});
    if (gLFadeA > 2.f && gLFadeR.width > 0.f)
        FillRR(gLFadeR, 0, (Color){23, 24, 28, U8(gLFadeA)});   /* = C_PANEL */
    if (gSB) FillRR(gSBR, 2, (Color){92, 95, 104, 255});
    if (gMediaHoverName[0] && gMediaHoverRect.width > 0.f) {
        Vector2 mouse = GetMousePosition();
        Font font = FontPx(0, PxOf(0, 13.f));
        Vector2 text = MeasureTextEx(font, gMediaHoverName,
                                     (float)font.baseSize / fmaxf(1.f, gDpiScale), 0.f);
        float tw = fminf(text.x, 330.f), th = fmaxf(22.f, text.y + 8.f);
        Rectangle tip = {mouse.x + 14.f, mouse.y + 16.f, tw + 18.f, th};
        if (tip.x + tip.width > GetScreenWidth() - 6.f) tip.x = mouse.x - tip.width - 12.f;
        if (tip.y + tip.height > GetScreenHeight() - 6.f) tip.y = mouse.y - tip.height - 12.f;
        FillRR(tip, 6.f, (Color){27, 28, 32, 245});
        StrokeRR(tip, 6.f, 1.f, (Color){68, 70, 78, 245});
        TxtAt(0, gMediaHoverName, tip.x + 9.f, tip.y + 4.f, 13.f, (Color){237, 237, 240, 255});
    }
}

static void DrawPreviewQualityDropdown(void) {
    float t = gPreviewMenuT;
    if (t < .01f || gPreviewQualityRect.width <= 0.f) return;
    const int options[] = {240, 360, 480, 720, 1080};
    Rectangle box = {gPreviewQualityRect.x + gPreviewQualityRect.width - 96.f,
                     gPreviewQualityRect.y - 166.f + 8.f * (1.f - t), 96.f, 160.f};
    Vector2 mouse = GetMousePosition();
    bool over = mouse.x >= box.x && mouse.x <= box.x + box.width && mouse.y >= box.y && mouse.y <= box.y + box.height;
    if (over && gPreviewMenuOpen) gHand = true;
    FillRR(box, 9.f, (Color){24, 25, 28, U8(255.f * t)});
    StrokeRR(box, 9.f, 1.f, (Color){54, 56, 62, U8(255.f * t)});
    for (int i = 0; i < 5; i++) {
        Rectangle row = {box.x + 4.f, box.y + 4.f + i * 30.f, box.width - 8.f, 28.f};
        bool rowHover = gPreviewMenuOpen && mouse.x >= row.x && mouse.x <= row.x + row.width && mouse.y >= row.y && mouse.y <= row.y + row.height;
        if (options[i] == gPreviewRes) FillRR(row, 6.f, (Color){45, 47, 53, U8(255.f * t)});
        else if (rowHover) FillRR(row, 6.f, (Color){37, 39, 44, U8(255.f * t)});
        char label[16]; snprintf(label, sizeof(label), "%dp", options[i]);
        Color txt = options[i] == gPreviewRes ? RC(C_TEXT) : RC(C_DIM); txt.a = U8(txt.a * t);
        TxtAt(0, label, row.x + 12.f, row.y + 6.f, 14.f, txt);
        if (options[i] == gPreviewRes) {
            Color check = (Color){224, 226, 231, U8(255.f * t)};
            DrawLineEx((Vector2){row.x + row.width - 17.f, row.y + 14.f}, (Vector2){row.x + row.width - 13.f, row.y + 18.f}, 1.6f, check);
            DrawLineEx((Vector2){row.x + row.width - 13.f, row.y + 18.f}, (Vector2){row.x + row.width - 7.f, row.y + 10.f}, 1.6f, check);
        }
    }
}

static void DrawPreviewFpsDropdown(void) {
    float t = gPreviewFpsMenuT;
    if (t < .01f || gPreviewFpsRect.width <= 0.f) return;
    const int options[] = {24, 30, 60};
    Rectangle box = {gPreviewFpsRect.x + gPreviewFpsRect.width - 88.f,
                     gPreviewFpsRect.y - 102.f + 8.f * (1.f - t), 88.f, 98.f};
    Vector2 mouse = GetMousePosition();
    bool over = mouse.x >= box.x && mouse.x <= box.x + box.width && mouse.y >= box.y && mouse.y <= box.y + box.height;
    if (over && gPreviewFpsMenuOpen) gHand = true;
    FillRR(box, 9.f, (Color){24, 25, 28, U8(255.f * t)});
    StrokeRR(box, 9.f, 1.f, (Color){54, 56, 62, U8(255.f * t)});
    for (int i = 0; i < 3; i++) {
        Rectangle row = {box.x + 4.f, box.y + 4.f + i * 30.f, box.width - 8.f, 28.f};
        bool rowHover = gPreviewFpsMenuOpen && mouse.x >= row.x && mouse.x <= row.x + row.width && mouse.y >= row.y && mouse.y <= row.y + row.height;
        if (options[i] == gPreviewFps) FillRR(row, 6.f, (Color){45, 47, 53, U8(255.f * t)});
        else if (rowHover) FillRR(row, 6.f, (Color){37, 39, 44, U8(255.f * t)});
        char label[16]; snprintf(label, sizeof(label), "%d fps", options[i]);
        Color txt = options[i] == gPreviewFps ? RC(C_TEXT) : RC(C_DIM); txt.a = U8(txt.a * t);
        TxtAt(0, label, row.x + 11.f, row.y + 6.f, 13.f, txt);
        if (options[i] == gPreviewFps) {
            Color check = (Color){224, 226, 231, U8(255.f * t)};
            DrawLineEx((Vector2){row.x + row.width - 17.f, row.y + 14.f}, (Vector2){row.x + row.width - 13.f, row.y + 18.f}, 1.6f, check);
            DrawLineEx((Vector2){row.x + row.width - 13.f, row.y + 18.f}, (Vector2){row.x + row.width - 7.f, row.y + 10.f}, 1.6f, check);
        }
    }
}

static Clay_Dimensions MeasureTxt(Clay_StringSlice t, Clay_TextElementConfig *cfg, void *u) {
    (void)u;
    char buf[512]; int n = t.length < 511 ? t.length : 511;
    memcpy(buf, t.chars, n); buf[n] = 0;
    int px = PxOf(cfg->fontId, (float)cfg->fontSize);
    Vector2 v = MeasureTextEx(FontPx(cfg->fontId, px), buf, (float)px / fmaxf(1.f, gDpiScale), cfg->letterSpacing);
    return (Clay_Dimensions){v.x, v.y};
}
static void OnClayError(Clay_ErrorData e) { printf("Clay error: %.*s\n", e.errorText.length, e.errorText.chars); }

