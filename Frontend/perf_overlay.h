/* perf_overlay.h - F3 preview timing overlay. Self-contained: include right after core.h.
   Needs gPreviewRes, gPreviewFps and gPreviewRequestStartedAt from core.h. */
#ifndef STELLA_PERF_OVERLAY_H
#define STELLA_PERF_OVERLAY_H

static void TxtAt(int font, const char *s, float x, float y, float size, Color c);
static bool PlutoJsonNumber(const char *json, const char *key, double *out);

static bool   gPerfOverlay;
static double gPerfQueueMs, gPerfSetupMs, gPerfDecodeMs, gPerfBackendMs, gPerfUploadMs, gPerfEndToEndMs;
static double gPerfFrameIntervalMs, gPerfLastShownAt, gPerfDisplayFps, gPerfFpsWindowAt, gPerfRingDepth;
static int    gPerfShownInWindow;
static unsigned gPerfShownFrames, gPerfDiscardedFrames;
static double gPerfRestarts, gPerfProxyClips, gPerfOriginalClips;

/* Call once per frame that is uploaded and displayed. */
static void PerfNoteFrame(const char *event, double now, double uploadStartedAt) {
    double q = 0, su = 0, dc = 0, bk = 0, rd = 0;
    PlutoJsonNumber(event, "queueMs", &q);
    PlutoJsonNumber(event, "streamSetupMs", &su);
    PlutoJsonNumber(event, "decodeWaitMs", &dc);
    PlutoJsonNumber(event, "totalMs", &bk);
    PlutoJsonNumber(event, "ringDepth", &rd);
    PlutoJsonNumber(event, "restarts", &gPerfRestarts);
    PlutoJsonNumber(event, "proxyClips", &gPerfProxyClips);
    PlutoJsonNumber(event, "originalClips", &gPerfOriginalClips);
    const double a = gPerfShownFrames ? 0.1 : 1.0;
    gPerfQueueMs += (q - gPerfQueueMs) * a;
    gPerfSetupMs += (su - gPerfSetupMs) * a;
    gPerfDecodeMs += (dc - gPerfDecodeMs) * a;
    gPerfBackendMs += (bk - gPerfBackendMs) * a;
    gPerfRingDepth += (rd - gPerfRingDepth) * a;
    gPerfUploadMs += ((now - uploadStartedAt) * 1000.0 - gPerfUploadMs) * a;
    gPerfEndToEndMs += ((now - gPreviewRequestStartedAt) * 1000.0 - gPerfEndToEndMs) * a;
    if (gPerfLastShownAt > 0.0)
        gPerfFrameIntervalMs += ((now - gPerfLastShownAt) * 1000.0 - gPerfFrameIntervalMs) * a;
    gPerfLastShownAt = now;
    gPerfShownFrames++; gPerfShownInWindow++;
    if (now - gPerfFpsWindowAt >= 1.0) {
        gPerfDisplayFps = gPerfShownInWindow / (now - gPerfFpsWindowAt);
        gPerfShownInWindow = 0; gPerfFpsWindowAt = now;
    }
}

static void DrawPerfOverlay(void) {
    if (!gPerfOverlay) return;
    char line[7][112];
    const double budget = 1000.0 / (double)(gPreviewFps > 0 ? gPreviewFps : 30);
    snprintf(line[0], sizeof line[0], "Preview %dp @ %dfps   shown %.1f fps   interval %.1f ms", gPreviewRes, gPreviewFps, gPerfDisplayFps, gPerfFrameIntervalMs);
    snprintf(line[1], sizeof line[1], "end-to-end %.1f ms (budget %.1f)", gPerfEndToEndMs, budget);
    snprintf(line[2], sizeof line[2], "queue %.1f   setup %.1f   decode wait %.1f", gPerfQueueMs, gPerfSetupMs, gPerfDecodeMs);
    snprintf(line[3], sizeof line[3], "backend %.1f   upload %.1f   decoded-ahead %.1f frames", gPerfBackendMs, gPerfUploadMs, gPerfRingDepth);
    snprintf(line[4], sizeof line[4], "frames shown %u   discarded %u", gPerfShownFrames, gPerfDiscardedFrames);
    snprintf(line[5], sizeof line[5], "stream restarts %.0f   source: %.0f proxy / %.0f ORIGINAL", gPerfRestarts, gPerfProxyClips, gPerfOriginalClips);
    snprintf(line[6], sizeof line[6], "F3 hides");
    const float w = 440.f, h = 16.f * 7 + 14.f;
    Rectangle box = {(float)GetScreenWidth() - w - 12.f, 44.f, w, h};
    DrawRectangleRec(box, (Color){0, 0, 0, 190});
    for (int i = 0; i < 7; i++) {
        Color c = (i == 1 && gPerfEndToEndMs > budget) ? (Color){255, 120, 100, 255} : (Color){225, 225, 230, 255};
        TxtAt(0, line[i], box.x + 8.f, box.y + 7.f + 16.f * i, 12.f, c);
    }
}
#endif
