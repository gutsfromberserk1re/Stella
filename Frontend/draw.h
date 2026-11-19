/* Pluto frontend drawing: raylib primitives, icons, custom elements and Clay renderer. */
/* ------------------------------------------------------------------ */
/* raylib drawing helpers                                              */
/* ------------------------------------------------------------------ */
static unsigned char U8(float x) { return (unsigned char)(x < 0 ? 0 : x > 255 ? 255 : x + .5f); }
static Color RC(Clay_Color c) { return (Color){U8(c.r), U8(c.g), U8(c.b), U8(c.a)}; }
static Color LerpC(Color a, Color b, float t) {
    return (Color){U8(a.r + (b.r - a.r) * t), U8(a.g + (b.g - a.g) * t), U8(a.b + (b.b - a.b) * t), U8(a.a + (b.a - a.a) * t)};
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
static void RadGrad(Vector2 c, float r, Color inner, Color outer) {
    for (float rr = r; rr > 0.5f; rr -= 1.f) DrawCircleV(c, rr, LerpC(inner, outer, rr / r));
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
    DrawTextEx(FontPx(font, px), s, (Vector2){roundf(x), roundf(y)}, (float)px, 0, c);
}
static float TxtW(int font, const char *s, float size) {
    int px = PxOf(font, (float)(int)(size + 2.25f));
    return MeasureTextEx(FontPx(font, px), s, (float)px, 0).x;
}

/* ------------------------------------------------------------------ */
/* icons                                                               */
/* ------------------------------------------------------------------ */
static Vector2 IconPoint(float x, float y, float w, float h, float nx, float ny, float rotation) {
    float dx = (nx - .5f) * w, dy = (ny - .5f) * h;
    float a = rotation * DEG2RAD, cs = cosf(a), sn = sinf(a);
    return (Vector2){x + .5f * w + dx * cs - dy * sn, y + .5f * h + dx * sn + dy * cs};
}

static void DrawIcon(int k, Rectangle r, Color c, float v) {
    float x = r.x, y = r.y, w = r.width, h = r.height;
    float t = fmaxf(1.3f, w / 11.0f);
#define P(a, b) IconPoint(x, y, w, h, a, b, k == I_RESET ? v : 0.f)
#define LN(a, b, c2, d) DrawLineEx(P(a, b), P(c2, d), t, c)
    switch (k) {
    case I_PLUS:    LN(.22, .5, .78, .5); if (v > 0.01f) LN(.5, .5 - .28 * v, .5, .5 + .28 * v); break;
    case I_CHEV_R:  LN(.38, .2, .66, .5); LN(.66, .5, .38, .8); break;
    case I_CHEV_L:  LN(.62, .2, .34, .5); LN(.34, .5, .62, .8); break;
    case I_CHEV_UP: LN(.25, .62, .5, .38); LN(.5, .38, .75, .62); break;
    case I_CHEV_DN: LN(.25, .38, .5, .62); LN(.5, .62, .75, .38); break;
    case I_SEARCH:  Ring(P(.44, .44), .28 * w, t, c); LN(.65, .65, .88, .88); break;
    case I_LIBRARY: StrokeRR((Rectangle){x + .1f * w, y + .15f * h, .8f * w, .7f * h}, .12f * w, t, c);
                    LN(.32, .15, .32, .85); LN(.68, .15, .68, .85); LN(.1, .5, .9, .5); break;
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
    case I_RESET:   Arc(P(.5, .5), .34f * w, t, 210 + v, 500 + v, c); LN(.2, .33, .2, .1); LN(.2, .33, .42, .33); break;
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
    case I_MAGNET:  Arc(P(.5, .5), .28f * w, t, 0, 180, c); LN(.22, .5, .22, .16); LN(.78, .5, .78, .16);
                    LN(.12, .2, .32, .2); LN(.68, .2, .88, .2); break;
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
    if (cu->kind < K_THUMB) { DrawIcon(cu->kind, r, RC(cu->a), cu->v); return; }
    switch (cu->kind) {
    case K_THUMB: {
        int style = cu->i & 15, idx = cu->i >> 4;
        GradRR(r, 10, RC(cu->a), RC(cu->b));
        if (style == 0) {
            DrawCircleV((Vector2){r.x + .72f * r.width, r.y + .30f * r.height}, r.height * .17f, (Color){250, 228, 150, 255});
            Color m = RC(cu->c);
            Tri((Vector2){r.x + .06f * r.width, r.y + r.height}, (Vector2){r.x + .30f * r.width, r.y + .40f * r.height}, (Vector2){r.x + .62f * r.width, r.y + r.height}, m);
            Tri((Vector2){r.x + .36f * r.width, r.y + r.height}, (Vector2){r.x + .64f * r.width, r.y + .48f * r.height}, (Vector2){r.x + .94f * r.width, r.y + r.height}, LerpC(m, WHITE, .10f));
            const char *lab = kMedia[idx].label;
            if (lab) TxtAt(1, lab, r.x + 7, r.y + r.height - 37, 6, (Color){240, 240, 244, 235});
        }
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
        float rad = fminf(r.width, r.height) / 2 - 1;
        DrawCircleV((Vector2){c.x, c.y + 1.5f}, rad + 1.f, (Color){0, 0, 0, 100});
        DrawCircleV(c, rad, LerpC((Color){52, 53, 59, 255}, (Color){78, 80, 86, 255}, cu->f));
        RadGrad(c, rad - 1.5f, (Color){47, 48, 54, 255}, (Color){20, 21, 24, 255});
        Ring(c, rad - 1.f, 1.f, (Color){91, 93, 99, 190});
        DrawEllipse((int)c.x, (int)(c.y - rad * .36f), rad * .54f, rad * .12f, (Color){255, 255, 255, 13});
        float ang = (cu->v - .5f) * 4.7f;
        Vector2 d = {sinf(ang), -cosf(ang)};
        DrawLineEx((Vector2){c.x + d.x * rad * .74f, c.y + d.y * rad * .74f}, (Vector2){c.x + d.x * rad * .88f, c.y + d.y * rad * .88f}, 3.f, (Color){228, 229, 233, 255});
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
        float dot = 8.5f + 1.5f * cu->f;
        DrawCircleV(p, dot + 3.f, (Color){10, 10, 14, 120});
        if (cu->v > .5f) {
            DrawCircleV(p, dot + 1.8f, (Color){244, 245, 248, 255});
            DrawCircleV(p, dot - .5f, RC(cu->c));
        } else {
            DrawCircleV(p, dot + 1.f, (Color){226, 227, 231, 255});
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
        const float pps = 21.5f;
        float bottom = r.y + r.height;
        for (int s = 0;; s++) {
            float px = r.x + s * pps;
            if (px > r.x + r.width) break;
            bool major = (s % 5 == 0);
            DrawLineEx((Vector2){px, bottom - (major ? 10 : 5)}, (Vector2){px, bottom}, 1.f, (Color){72, 75, 84, 255});
            if (major) { char b[16]; snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60); TxtAt(0, b, px + 3, r.y + 3, 12, RC(C_MUTED)); }
        }
        gPH = true; gPHPos = (Vector2){r.x, r.y + 4}; gPHLen = r.height - 4 + cu->f;
    } break;

    case K_PREVIEW: {
        DrawRectangleRec(r, BLACK);
        DrawRectangleLinesEx(r, 1.5f, WHITE);
        Vector2 q[4] = {{r.x, r.y}, {r.x + r.width, r.y}, {r.x, r.y + r.height}, {r.x + r.width, r.y + r.height}};
        for (int i = 0; i < 4; i++) { DrawCircleV(q[i], 5.5f, WHITE); }
    } break;

    case K_CLIP: {
        GradRR(r, 6, RC(cu->a), RC(cu->b));
        float cy = r.y + r.height - 9;
        for (float px = r.x + 6; px < r.x + r.width - 6; px += 3.f) {
            float hh = (.2f + .8f * fabsf(sinf(px * .53f) * cosf(px * .17f))) * 11.f;
            DrawLineEx((Vector2){px, cy - hh / 2}, (Vector2){px, cy + hh / 2}, 1.4f, (Color){226, 205, 240, 170});
        }
        FillRR(r, 6, (Color){255, 255, 255, U8(18 * cu->f)});
        StrokeRR(r, 6, 2, (Color){245, 245, 250, 255});
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
            DrawTextEx(FontPx(d->fontId, px), buf, (Vector2){roundf(r.x), roundf(r.y)}, (float)px, d->letterSpacing, RC(d->textColor));
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
    if (gPH) {
        Color red = {240, 62, 72, 255};
        DrawLineEx(gPHPos, (Vector2){gPHPos.x, gPHPos.y + gPHLen}, 1.6f, red);
        DrawRectangleRec((Rectangle){gPHPos.x - 5, gPHPos.y - 4, 10, 8}, red);
        Tri((Vector2){gPHPos.x - 5, gPHPos.y + 4}, (Vector2){gPHPos.x + 5, gPHPos.y + 4}, (Vector2){gPHPos.x, gPHPos.y + 10}, red);
    }
    if (gFadeA > 2.f && gFadeR.width > 0.f)
        GradRR(gFadeR, 22, (Color){28, 29, 32, U8(gFadeA)}, (Color){21, 22, 24, U8(gFadeA)});
    if (gLFadeA > 2.f && gLFadeR.width > 0.f)
        FillRR(gLFadeR, 0, (Color){23, 24, 28, U8(gLFadeA)});   /* = C_PANEL */
    if (gSB) FillRR(gSBR, 2, (Color){92, 95, 104, 255});
}

static Clay_Dimensions MeasureTxt(Clay_StringSlice t, Clay_TextElementConfig *cfg, void *u) {
    (void)u;
    char buf[512]; int n = t.length < 511 ? t.length : 511;
    memcpy(buf, t.chars, n); buf[n] = 0;
    int px = PxOf(cfg->fontId, (float)cfg->fontSize);
    Vector2 v = MeasureTextEx(FontPx(cfg->fontId, px), buf, (float)px, cfg->letterSpacing);
    return (Clay_Dimensions){v.x, v.y};
}
static void OnClayError(Clay_ErrorData e) { printf("Clay error: %.*s\n", e.errorText.length, e.errorText.chars); }

