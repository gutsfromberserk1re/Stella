#ifndef PLUTO_BACKEND_BRIDGE_H
#define PLUTO_BACKEND_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool gTimelinePropertyEditOpen;

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>

typedef char *(__cdecl *PlutoBackendRequestFn)(const char *json);
typedef uint64_t(__cdecl *PlutoBackendSubmitFn)(const char *json);
typedef char *(__cdecl *PlutoBackendPollFn)(void);
typedef void(__cdecl *PlutoBackendFreeFn)(char *value);
typedef int32_t(__cdecl *PlutoBackendCopyPreviewFn)(uint64_t requestId, uint8_t *destination, size_t capacity,
                                                    uint32_t *width, uint32_t *height, size_t *byteLength, double *at);
typedef int32_t(__cdecl *PlutoBackendTakePreviewFn)(uint64_t requestId, const uint8_t **pixels, size_t *byteLength,
                                                    uint32_t *width, uint32_t *height, double *at, void **owner);
typedef void(__cdecl *PlutoBackendReleasePreviewPixelsFn)(void *owner);
typedef int32_t(__cdecl *PlutoBackendSetGLShareContextFn)(void *context, void *deviceContext);
typedef int32_t(__cdecl *PlutoBackendDisableGLShareFn)(void);
typedef int32_t(__cdecl *PlutoBackendTakeGLPreviewFn)(uint64_t requestId, uint32_t *textureId,
    uint32_t *width, uint32_t *height, double *at, uint64_t *slotToken, void **readyFence);
typedef int32_t(__cdecl *PlutoBackendDiscardGLPreviewFn)(uint64_t requestId);
typedef int32_t(__cdecl *PlutoBackendFinishGLPreviewFn)(uint64_t slotToken);
typedef int32_t(__cdecl *PlutoBackendReleaseGLPreviewFencedFn)(uint64_t slotToken, void *uiFence);
typedef int32_t(__cdecl *PlutoBackendReleaseGLPreviewFn)(uint64_t slotToken);

typedef struct {
    HMODULE module;
    PlutoBackendRequestFn request;
    PlutoBackendSubmitFn submit;
    PlutoBackendPollFn poll;
    PlutoBackendFreeFn release;
    PlutoBackendCopyPreviewFn copyPreview;
    PlutoBackendTakePreviewFn takePreview;
    PlutoBackendReleasePreviewPixelsFn releasePreviewPixels;
    PlutoBackendSetGLShareContextFn setGlShareContext;
    PlutoBackendDisableGLShareFn disableGlShare;
    PlutoBackendTakeGLPreviewFn takeGlPreview;
    PlutoBackendDiscardGLPreviewFn discardGlPreview;
    PlutoBackendFinishGLPreviewFn finishGlPreview;
    PlutoBackendReleaseGLPreviewFencedFn releaseGlPreviewFenced;
    PlutoBackendReleaseGLPreviewFn releaseGlPreview;
} PlutoBackend;

static PlutoBackend gBackend;
static void PlutoHydrateInspector(const char *clipBegin, const char *clipEnd);
static bool PlutoTimelineSetPropertiesJson(const char *propertiesJson);

static bool PlutoBackendLoad(void) {
    if (gBackend.module) return true;
    char modulePath[MAX_PATH] = {0};
    const char *appDir = GetApplicationDirectory();
    snprintf(modulePath, sizeof(modulePath), "%spluto_backend.dll", appDir);
    gBackend.module = LoadLibraryA(modulePath);
    if (!gBackend.module) gBackend.module = LoadLibraryA("pluto_backend.dll");
    if (!gBackend.module) return false;
    gBackend.request = (PlutoBackendRequestFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_request");
    gBackend.submit = (PlutoBackendSubmitFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_submit");
    gBackend.poll = (PlutoBackendPollFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_poll");
    gBackend.release = (PlutoBackendFreeFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_free");
    gBackend.copyPreview = (PlutoBackendCopyPreviewFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_copy_preview_frame");
    gBackend.takePreview = (PlutoBackendTakePreviewFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_take_preview_frame");
    gBackend.releasePreviewPixels = (PlutoBackendReleasePreviewPixelsFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_release_preview_pixels");
    gBackend.setGlShareContext = (PlutoBackendSetGLShareContextFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_set_gl_share_context");
    gBackend.disableGlShare = (PlutoBackendDisableGLShareFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_disable_gl_share");
    gBackend.takeGlPreview = (PlutoBackendTakeGLPreviewFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_take_gl_preview_frame");
    gBackend.discardGlPreview = (PlutoBackendDiscardGLPreviewFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_discard_gl_preview_frame");
    gBackend.finishGlPreview = (PlutoBackendFinishGLPreviewFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_finish_gl_preview_frame");
    gBackend.releaseGlPreviewFenced = (PlutoBackendReleaseGLPreviewFencedFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_release_gl_preview_frame_fenced");
    gBackend.releaseGlPreview = (PlutoBackendReleaseGLPreviewFn)(void *)GetProcAddress(gBackend.module, "pluto_backend_release_gl_preview_frame");
    if (!gBackend.request || !gBackend.submit || !gBackend.poll || !gBackend.release || !gBackend.copyPreview) {
        FreeLibrary(gBackend.module);
        gBackend = (PlutoBackend){0};
        return false;
    }
    return true;
}

static bool PlutoBackendShareHostGLContext(void) {
#ifdef _WIN32
    if (!gBackend.setGlShareContext) return false;
    HGLRC context = wglGetCurrentContext();
    HDC deviceContext = wglGetCurrentDC();
    return context && deviceContext &&
        gBackend.setGlShareContext((void *)context, (void *)deviceContext) == 1;
#else
    return false;
#endif
}

static bool PlutoJsonString(const char *json, const char *key, char *out, size_t cap) {
    char needle[96];
    if (snprintf(needle, sizeof(needle), "\"%s\":\"", key) <= 0) return false;
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    size_t n = 0;
    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') {
            c = (unsigned char)*p++;
            if (!c) return false;
            if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
            else if (c == 'u') { if (!*p) return false; p += 4; c = '?'; }
        }
        if (n + 1 < cap) out[n++] = (char)c;
    }
    if (*p != '"' || cap == 0) return false;
    out[n] = 0;
    return true;
}

static bool PlutoJsonNumber(const char *json, const char *key, double *out) {
    char needle[96];
    if (snprintf(needle, sizeof(needle), "\"%s\":", key) <= 0) return false;
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    char *end = NULL;
    double value = strtod(p, &end);
    if (end == p) return false;
    *out = value;
    return true;
}

static int PlutoFindPreviewRequest(uint64_t requestId) {
    for (int i = 0; i < STELLA_PREVIEW_REQUESTS; i++)
        if (gPreviewRequests[i].id == requestId && requestId) return i;
    return -1;
}

static bool PlutoPreviewPathInFlight(const char *path) {
    if (!path || !*path) return false;
    for (int i = 0; i < STELLA_PREVIEW_REQUESTS; i++)
        if (gPreviewRequests[i].id && gPreviewRequests[i].textPath[0] &&
            strcmp(gPreviewRequests[i].textPath, path) == 0) return true;
    return false;
}

static void PlutoRefreshPreviewRequestMirrors(void) {
    gPreviewTaskId = 0;
    gPreviewTextPathInFlight[0] = 0;
    for (int i = 0; i < STELLA_PREVIEW_REQUESTS; i++) {
        StellaPreviewRequest *request = &gPreviewRequests[i];
        if (!request->id) continue;
        if (request->id > gPreviewTaskId) gPreviewTaskId = request->id;
        if (!gPreviewTextPathInFlight[0] && request->textPath[0])
            snprintf(gPreviewTextPathInFlight, sizeof(gPreviewTextPathInFlight), "%s", request->textPath);
    }
}

static bool PlutoCompletePreviewRequest(uint64_t requestId, StellaPreviewRequest *completed) {
    int slot = PlutoFindPreviewRequest(requestId);
    if (slot < 0) return false;
    if (completed) *completed = gPreviewRequests[slot];
    memset(&gPreviewRequests[slot], 0, sizeof(gPreviewRequests[slot]));
    if (gPreviewInFlightCount > 0) gPreviewInFlightCount--;
    PlutoRefreshPreviewRequestMirrors();
    return true;
}


/* CPU-readback fallback: upload pixels through a small PBO ring so the driver
   can transfer bytes asynchronously instead of forcing UpdateTexture to wait
   on the UI thread. The preferred WGL shared-texture path bypasses this block. */
#if defined(_WIN32)
typedef unsigned int StellaGLenum;
typedef unsigned int StellaGLuint;
typedef int StellaGLint;
typedef int StellaGLsizei;
typedef unsigned int StellaGLbitfield;
typedef ptrdiff_t StellaGLsizeiptr;
typedef void *StellaGLsync;
typedef void (APIENTRY *StellaPFNGLGENBUFFERSPROC)(StellaGLsizei, StellaGLuint *);
typedef void (APIENTRY *StellaPFNGLDELETEBUFFERSPROC)(StellaGLsizei, const StellaGLuint *);
typedef void (APIENTRY *StellaPFNGLBINDBUFFERPROC)(StellaGLenum, StellaGLuint);
typedef void (APIENTRY *StellaPFNGLBUFFERDATAPROC)(StellaGLenum, StellaGLsizeiptr, const void *, StellaGLenum);
typedef StellaGLsync (APIENTRY *StellaPFNGLFENCESYNCPROC)(StellaGLenum, StellaGLbitfield);
typedef StellaGLenum (APIENTRY *StellaPFNGLCLIENTWAITSYNCPROC)(StellaGLsync, StellaGLbitfield, uint64_t);
typedef void (APIENTRY *StellaPFNGLDELETESYNCPROC)(StellaGLsync);
typedef void (APIENTRY *StellaPFNGLGETINTEGERVPROC)(StellaGLenum, StellaGLint *);
typedef void (APIENTRY *StellaPFNGLBINDTEXTUREPROC)(StellaGLenum, StellaGLuint);
typedef void (APIENTRY *StellaPFNGLTEXSUBIMAGE2DPROC)(StellaGLenum, StellaGLint, StellaGLint, StellaGLint,
                                                       StellaGLsizei, StellaGLsizei, StellaGLenum, StellaGLenum, const void *);
typedef void (APIENTRY *StellaPFNGLACTIVETEXTUREPROC)(StellaGLenum);
typedef void (APIENTRY *StellaPFNGLPIXELSTOREIPROC)(StellaGLenum, StellaGLint);

enum {
    STELLA_GL_PIXEL_UNPACK_BUFFER = 0x88EC,
    STELLA_GL_PIXEL_UNPACK_BUFFER_BINDING = 0x88EF,
    STELLA_GL_STREAM_DRAW = 0x88E0,
    STELLA_GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117,
    STELLA_GL_ALREADY_SIGNALED = 0x911A,
    STELLA_GL_CONDITION_SATISFIED = 0x911C,
    STELLA_GL_TIMEOUT_EXPIRED = 0x911B,
    STELLA_GL_WAIT_FAILED = 0x911D,
    STELLA_GL_TEXTURE0 = 0x84C0,
    STELLA_GL_ACTIVE_TEXTURE = 0x84E0,
    STELLA_GL_TEXTURE_BINDING_2D = 0x8069,
    STELLA_GL_TEXTURE_2D = 0x0DE1,
    STELLA_GL_RGBA = 0x1908,
    STELLA_GL_UNSIGNED_BYTE = 0x1401,
    STELLA_GL_UNPACK_ALIGNMENT = 0x0CF5
};

typedef struct {
    StellaGLuint buffer;
    StellaGLsync fence;
    size_t capacity;
    bool poisoned;
} StellaPreviewPboSlot;

static StellaPreviewPboSlot gPreviewPboSlots[3];
static unsigned gPreviewPboNext;
static bool gPreviewPboFunctionsLoaded;
static bool gPreviewPboFunctionsAvailable;
static StellaPFNGLGENBUFFERSPROC stella_glGenBuffers;
static StellaPFNGLDELETEBUFFERSPROC stella_glDeleteBuffers;
static StellaPFNGLBINDBUFFERPROC stella_glBindBuffer;
static StellaPFNGLBUFFERDATAPROC stella_glBufferData;
static StellaPFNGLFENCESYNCPROC stella_glFenceSync;
static StellaPFNGLCLIENTWAITSYNCPROC stella_glClientWaitSync;
static StellaPFNGLDELETESYNCPROC stella_glDeleteSync;
static StellaPFNGLGETINTEGERVPROC stella_glGetIntegerv;
static StellaPFNGLBINDTEXTUREPROC stella_glBindTexture;
static StellaPFNGLTEXSUBIMAGE2DPROC stella_glTexSubImage2D;
static StellaPFNGLACTIVETEXTUREPROC stella_glActiveTexture;
static StellaPFNGLPIXELSTOREIPROC stella_glPixelStorei;
typedef void (APIENTRY *StellaPFNGLWAITSYNCPROC)(StellaGLsync, StellaGLbitfield, uint64_t);
typedef unsigned char (APIENTRY *StellaPFNGLISSYNCPROC)(StellaGLsync);
typedef unsigned char (APIENTRY *StellaPFNGLISTEXTUREPROC)(StellaGLuint);
typedef void (APIENTRY *StellaPFNGLFLUSHPROC)(void);
typedef void (APIENTRY *StellaPFNGLFINISHPROC)(void);
static StellaPFNGLWAITSYNCPROC stella_glWaitSync;
static StellaPFNGLISSYNCPROC stella_glIsSync;
static StellaPFNGLISTEXTUREPROC stella_glIsTexture;
static StellaPFNGLFLUSHPROC stella_glFlush;
static StellaPFNGLFINISHPROC stella_glFinish;
static uint64_t gPreviewFrameGlSlot;
static uint64_t gPreviewPendingGlReleaseSlots[3];
static int gPreviewPendingGlReleaseCount;

static PROC PlutoGetGlProc(const char *name) {
    PROC proc = wglGetProcAddress(name);
    if (!proc || proc == (PROC)1 || proc == (PROC)2 || proc == (PROC)3 || proc == (PROC)-1) {
        HMODULE gl = GetModuleHandleA("opengl32.dll");
        proc = gl ? GetProcAddress(gl, name) : NULL;
    }
    return proc;
}

static void PlutoLoadPreviewPboFunctions(void) {
    if (gPreviewPboFunctionsLoaded) return;
    gPreviewPboFunctionsLoaded = true;
#define STELLA_GL_LOAD(dst, type, name) dst = (type)(void *)PlutoGetGlProc(name)
    STELLA_GL_LOAD(stella_glGenBuffers, StellaPFNGLGENBUFFERSPROC, "glGenBuffers");
    STELLA_GL_LOAD(stella_glDeleteBuffers, StellaPFNGLDELETEBUFFERSPROC, "glDeleteBuffers");
    STELLA_GL_LOAD(stella_glBindBuffer, StellaPFNGLBINDBUFFERPROC, "glBindBuffer");
    STELLA_GL_LOAD(stella_glBufferData, StellaPFNGLBUFFERDATAPROC, "glBufferData");
    STELLA_GL_LOAD(stella_glFenceSync, StellaPFNGLFENCESYNCPROC, "glFenceSync");
    STELLA_GL_LOAD(stella_glWaitSync, StellaPFNGLWAITSYNCPROC, "glWaitSync");
    STELLA_GL_LOAD(stella_glIsSync, StellaPFNGLISSYNCPROC, "glIsSync");
    STELLA_GL_LOAD(stella_glIsTexture, StellaPFNGLISTEXTUREPROC, "glIsTexture");
    STELLA_GL_LOAD(stella_glFlush, StellaPFNGLFLUSHPROC, "glFlush");
    STELLA_GL_LOAD(stella_glFinish, StellaPFNGLFINISHPROC, "glFinish");
    STELLA_GL_LOAD(stella_glClientWaitSync, StellaPFNGLCLIENTWAITSYNCPROC, "glClientWaitSync");
    STELLA_GL_LOAD(stella_glDeleteSync, StellaPFNGLDELETESYNCPROC, "glDeleteSync");
    STELLA_GL_LOAD(stella_glGetIntegerv, StellaPFNGLGETINTEGERVPROC, "glGetIntegerv");
    STELLA_GL_LOAD(stella_glBindTexture, StellaPFNGLBINDTEXTUREPROC, "glBindTexture");
    STELLA_GL_LOAD(stella_glTexSubImage2D, StellaPFNGLTEXSUBIMAGE2DPROC, "glTexSubImage2D");
    STELLA_GL_LOAD(stella_glActiveTexture, StellaPFNGLACTIVETEXTUREPROC, "glActiveTexture");
    STELLA_GL_LOAD(stella_glPixelStorei, StellaPFNGLPIXELSTOREIPROC, "glPixelStorei");
#undef STELLA_GL_LOAD
    gPreviewPboFunctionsAvailable = stella_glGenBuffers && stella_glDeleteBuffers && stella_glBindBuffer &&
        stella_glBufferData && stella_glFenceSync && stella_glClientWaitSync && stella_glDeleteSync &&
        stella_glGetIntegerv && stella_glBindTexture && stella_glTexSubImage2D && stella_glActiveTexture;
}

static bool PlutoUploadPreviewTexturePbo(Texture2D texture, const uint8_t *pixels, size_t byteCount) {
    PlutoLoadPreviewPboFunctions();
    if (!gPreviewPboFunctionsAvailable || !texture.id || !pixels || byteCount == 0) return false;

    /* Poll fences without waiting. A busy ring falls back to Raylib's upload
       rather than stalling the UI thread. */
    StellaPreviewPboSlot *slot = NULL;
    for (unsigned offset = 0; offset < 3; offset++) {
        unsigned index = (gPreviewPboNext + offset) % 3;
        StellaPreviewPboSlot *candidate = &gPreviewPboSlots[index];
        if (candidate->poisoned) continue;
        if (candidate->fence) {
            StellaGLenum wait = stella_glClientWaitSync(candidate->fence, 0, 0);
            if (wait == STELLA_GL_ALREADY_SIGNALED || wait == STELLA_GL_CONDITION_SATISFIED) {
                stella_glDeleteSync(candidate->fence);
                candidate->fence = NULL;
            } else {
                continue;
            }
        }
        slot = candidate;
        gPreviewPboNext = (index + 1) % 3;
        break;
    }
    if (!slot) return false;

    if (!slot->buffer) stella_glGenBuffers(1, &slot->buffer);
    if (!slot->buffer) return false;

    StellaGLint previousActive = STELLA_GL_TEXTURE0;
    StellaGLint previousTexture = 0;
    StellaGLint previousPbo = 0;
    StellaGLint previousUnpackAlignment = 4;
    stella_glGetIntegerv(STELLA_GL_ACTIVE_TEXTURE, &previousActive);
    stella_glActiveTexture(STELLA_GL_TEXTURE0);
    stella_glGetIntegerv(STELLA_GL_TEXTURE_BINDING_2D, &previousTexture);
    stella_glGetIntegerv(STELLA_GL_PIXEL_UNPACK_BUFFER_BINDING, &previousPbo);
    stella_glGetIntegerv(STELLA_GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    stella_glBindTexture(STELLA_GL_TEXTURE_2D, texture.id);
    stella_glBindBuffer(STELLA_GL_PIXEL_UNPACK_BUFFER, slot->buffer);
    /* Orphaning gives the driver fresh storage if an older transfer is still
       consuming the previous allocation associated with this PBO name. */
    stella_glBufferData(STELLA_GL_PIXEL_UNPACK_BUFFER, (StellaGLsizeiptr)byteCount, pixels, STELLA_GL_STREAM_DRAW);
    slot->capacity = byteCount;
    if (stella_glPixelStorei) stella_glPixelStorei(STELLA_GL_UNPACK_ALIGNMENT, 1);
    stella_glTexSubImage2D(STELLA_GL_TEXTURE_2D, 0, 0, 0, texture.width, texture.height,
                           STELLA_GL_RGBA, STELLA_GL_UNSIGNED_BYTE, NULL);
    slot->fence = stella_glFenceSync(STELLA_GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    if (stella_glPixelStorei) stella_glPixelStorei(STELLA_GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    stella_glBindBuffer(STELLA_GL_PIXEL_UNPACK_BUFFER, (StellaGLuint)previousPbo);
    stella_glBindTexture(STELLA_GL_TEXTURE_2D, (StellaGLuint)previousTexture);
    stella_glActiveTexture((StellaGLenum)previousActive);
    if (!slot->fence) {
        /* Without a completion fence we cannot safely recycle this storage.
           Quarantine the slot and let the caller use the synchronous fallback. */
        slot->poisoned = true;
        return false;
    }
    return true;
}

static void PlutoReleasePreviewPbos(void) {
    if (!gPreviewPboFunctionsAvailable) return;
    for (int i = 0; i < 3; i++) {
        if (gPreviewPboSlots[i].fence) stella_glDeleteSync(gPreviewPboSlots[i].fence);
        if (gPreviewPboSlots[i].buffer) stella_glDeleteBuffers(1, &gPreviewPboSlots[i].buffer);
        memset(&gPreviewPboSlots[i], 0, sizeof(gPreviewPboSlots[i]));
    }
}
#else
static bool PlutoUploadPreviewTexturePbo(Texture2D texture, const uint8_t *pixels, size_t byteCount) {
    (void)texture; (void)pixels; (void)byteCount; return false;
}
static void PlutoReleasePreviewPbos(void) { }
#endif

#ifdef _WIN32
static void PlutoQueueSharedPreviewRelease(uint64_t slotToken) {
    if (!slotToken || !gBackend.releaseGlPreview) return;
    if (gPreviewPendingGlReleaseCount < 3) {
        gPreviewPendingGlReleaseSlots[gPreviewPendingGlReleaseCount++] = slotToken;
        return;
    }
    /* Never recycle a shared texture unless its draw completion can be
       acknowledged. With a three-slot renderer this indicates an ownership
       invariant failure; retain the slot rather than risk a write/read race. */
    OutputDebugStringA("Stella: shared preview release queue is full; retaining texture slot\n");
}

static void PlutoAfterDraw(void) {
    if (gPreviewPendingGlReleaseCount <= 0 || !gBackend.releaseGlPreview) return;
    PlutoLoadPreviewPboFunctions();
    int remaining = 0;
    for (int i = 0; i < gPreviewPendingGlReleaseCount; i++) {
        uint64_t token = gPreviewPendingGlReleaseSlots[i];
        bool released = false;
        if (stella_glFenceSync && stella_glFlush && gBackend.releaseGlPreviewFenced) {
            StellaGLsync fence = stella_glFenceSync(STELLA_GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (fence) {
                stella_glFlush();
                released = gBackend.releaseGlPreviewFenced(token, fence) == 1;
                if (!released && stella_glDeleteSync) stella_glDeleteSync(fence);
            }
        }
        if (!released) {
            /* Some WGL implementations share textures but not GLsync names.
               Complete the host's sampling commands before releasing the slot. */
            if (!stella_glFinish) {
                gPreviewPendingGlReleaseSlots[remaining++] = token;
                continue;
            }
            stella_glFinish();
            released = gBackend.releaseGlPreview(token) == 1;
        }
        if (!released) gPreviewPendingGlReleaseSlots[remaining++] = token;
    }
    for (int i = remaining; i < 3; i++) gPreviewPendingGlReleaseSlots[i] = 0;
    gPreviewPendingGlReleaseCount = remaining;
}

static void PlutoRetireCurrentPreviewTexture(void) {
    if (gPreviewFrameGlSlot) {
        PlutoQueueSharedPreviewRelease(gPreviewFrameGlSlot);
        gPreviewFrameGlSlot = 0;
    } else if (gPreviewFrame.id) {
        UnloadTexture(gPreviewFrame);
    }
    gPreviewFrame = (Texture2D){0};
}
#else
static void PlutoQueueSharedPreviewRelease(uint64_t slotToken) { (void)slotToken; }
static void PlutoAfterDraw(void) { }
static void PlutoRetireCurrentPreviewTexture(void) {
    if (gPreviewFrame.id) UnloadTexture(gPreviewFrame);
    gPreviewFrame = (Texture2D){0};
}
#endif

static bool PlutoUploadPreviewTexture(Texture2D texture, const uint8_t *pixels, size_t byteCount) {
    if (PlutoUploadPreviewTexturePbo(texture, pixels, byteCount)) return true;
    UpdateTexture(texture, pixels);
    return true;
}

static bool PlutoUploadPreviewFrame(uint64_t requestId, double *displayedAt) {
    if (!requestId) return false;
#ifdef _WIN32
    /* Preferred path: the backend returns a native GL texture name. No CPU
       pixels cross the ABI. Wait on WGPU's producer fence only if glIsSync
       confirms it is visible in this context; otherwise finish on WGPU's side. */
    if (gBackend.takeGlPreview) {
        PlutoLoadPreviewPboFunctions();
        uint32_t glTexture = 0, glWidth = 0, glHeight = 0;
        uint64_t slotToken = 0;
        double glAt = 0.0;
        void *readyFence = NULL;
        int32_t takenGl = gBackend.takeGlPreview(requestId, &glTexture, &glWidth, &glHeight,
                                                 &glAt, &slotToken, &readyFence);
        if (takenGl == 1) {
            if (!glTexture || !glWidth || !glHeight || !slotToken || !readyFence) {
                /* A malformed shared-frame descriptor must not release a texture
                   until the producer context confirms its writes have completed. */
                if (gBackend.finishGlPreview && slotToken && gBackend.finishGlPreview(slotToken) == 1 &&
                    gBackend.releaseGlPreview) {
                    gBackend.releaseGlPreview(slotToken);
                }
                return false;
            }
            /* Verify the native name exists in Raylib's context before using it.
               A WGL context creation/share failure must degrade to readback,
               never sample an unrelated object with a colliding GLuint. */
            if (!stella_glIsTexture || !stella_glIsTexture(glTexture)) {
                bool producerFinished = gBackend.finishGlPreview &&
                    gBackend.finishGlPreview(slotToken) == 1;
                if (producerFinished && gBackend.releaseGlPreview)
                    gBackend.releaseGlPreview(slotToken);
                if (gBackend.disableGlShare) gBackend.disableGlShare();
                OutputDebugStringA("Stella: WGPU texture is not visible in Raylib's GL namespace; disabling shared preview and using readback\n");
                return false;
            }
            bool syncVisible = stella_glIsSync && stella_glWaitSync &&
                stella_glIsSync((StellaGLsync)readyFence) != 0;
            if (syncVisible) {
                stella_glWaitSync((StellaGLsync)readyFence, 0, UINT64_MAX);
            } else if (!gBackend.finishGlPreview || gBackend.finishGlPreview(slotToken) != 1) {
                /* If synchronization cannot be established, intentionally retain
                   the slot. Releasing it early could race an unfinished GPU write. */
                return false;
            }
            PlutoRetireCurrentPreviewTexture();
            gPreviewFrame.id = glTexture;
            gPreviewFrame.width = (int)glWidth;
            gPreviewFrame.height = (int)glHeight;
            gPreviewFrame.mipmaps = 1;
            gPreviewFrame.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            gPreviewFrameGlSlot = slotToken;
            SetTextureFilter(gPreviewFrame, TEXTURE_FILTER_BILINEAR);
            if (displayedAt) *displayedAt = glAt;
            return true;
        }
    }
#endif
    uint32_t width = 0, height = 0;
    size_t required = 0;
    double at = 0.0;

    /* Fast path: transfer the backend-owned frame allocation directly to the
       synchronous Raylib upload. This avoids copying 3.7-8 MB into a second C
       buffer before UpdateTexture/LoadTextureFromImage. */
    if (gBackend.takePreview && gBackend.releasePreviewPixels) {
        const uint8_t *pixels = NULL;
        void *owner = NULL;
        int32_t taken = gBackend.takePreview(requestId, &pixels, &required,
                                             &width, &height, &at, &owner);
        if (taken != 1 || !pixels || !owner || !width || !height ||
            required != (size_t)width * height * 4u) {
            if (owner) gBackend.releasePreviewPixels(owner);
            return false;
        }
        bool uploaded = true;
        if (gPreviewFrame.id && !gPreviewFrameGlSlot && gPreviewFrame.width == (int)width && gPreviewFrame.height == (int)height) {
            PlutoUploadPreviewTexture(gPreviewFrame, pixels, required);
        } else {
            Image image = {(void *)pixels, (int)width, (int)height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            Texture2D next = LoadTextureFromImage(image);
            if (!next.id) uploaded = false;
            else {
                PlutoRetireCurrentPreviewTexture();
                gPreviewFrame = next;
                SetTextureFilter(gPreviewFrame, TEXTURE_FILTER_BILINEAR);
            }
        }
        gBackend.releasePreviewPixels(owner);
        if (!uploaded) return false;
        if (displayedAt) *displayedAt = at;
        return true;
    }

    /* Compatibility fallback for older backend DLLs that only expose copy. */
    if (!gBackend.copyPreview) return false;
    int32_t copied = gBackend.copyPreview(requestId, gPreviewPixels, gPreviewPixelCapacity,
                                          &width, &height, &required, &at);
    if (copied == -1 && required > 0) {
        uint8_t *next = (uint8_t *)realloc(gPreviewPixels, required);
        if (!next) return false;
        gPreviewPixels = next;
        gPreviewPixelCapacity = required;
        copied = gBackend.copyPreview(requestId, gPreviewPixels, gPreviewPixelCapacity,
                                      &width, &height, &required, &at);
    }
    if (copied != 1 || !width || !height || required != (size_t)width * height * 4u) return false;
    if (gPreviewFrame.id && !gPreviewFrameGlSlot && gPreviewFrame.width == (int)width && gPreviewFrame.height == (int)height) {
        PlutoUploadPreviewTexture(gPreviewFrame, gPreviewPixels, required);
    } else {
        Image image = {gPreviewPixels, (int)width, (int)height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        Texture2D next = LoadTextureFromImage(image);
        if (!next.id) return false;
        PlutoRetireCurrentPreviewTexture();
        gPreviewFrame = next;
        SetTextureFilter(gPreviewFrame, TEXTURE_FILTER_BILINEAR);
    }
    if (displayedAt) *displayedAt = at;
    return true;
}

static const char *PlutoJsonField(const char *begin, const char *end, const char *key) {
    char needle[96];
    int n = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return NULL;
    const char *p = begin;
    while (p && p < end) {
        p = strstr(p, needle);
        if (!p || p >= end) return NULL;
        return p + n;
    }
    return NULL;
}

static bool PlutoJsonStringRange(const char *begin, const char *end, const char *key, char *out, size_t cap) {
    const char *p = PlutoJsonField(begin, end, key);
    if (!p || p >= end || *p != '"' || cap == 0) return false;
    p++;
    size_t n = 0;
    while (p < end && *p && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') {
            if (p >= end) return false;
            c = (unsigned char)*p++;
            if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
            else if (c == 'u') { if (end - p < 4) return false; p += 4; c = '?'; }
        }
        if (n + 1 < cap) out[n++] = (char)c;
    }
    if (p >= end || *p != '"') return false;
    out[n] = 0;
    return true;
}

static bool PlutoJsonNumberRange(const char *begin, const char *end, const char *key, double *out) {
    const char *p = PlutoJsonField(begin, end, key);
    if (!p || p >= end) return false;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    char *tail = NULL;
    double value = strtod(p, &tail);
    if (tail == p || tail > end) return false;
    *out = value;
    return true;
}

static bool PlutoJsonMemberRange(const char *begin, const char *end, const char *key,
                                 const char **valueBegin, const char **valueEnd) {
    const char *p = PlutoJsonField(begin, end, key);
    if (!p || p >= end) return false;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (p >= end) return false;
    const char *q = p;
    if (*p == '"') {
        q = p + 1;
        bool escaped = false;
        while (q < end) {
            if (escaped) escaped = false;
            else if (*q == '\\') escaped = true;
            else if (*q == '"') { q++; break; }
            q++;
        }
        if (q > end || q[-1] != '"') return false;
    } else if (*p == '{' || *p == '[') {
        int braces = 0, brackets = 0;
        bool inString = false, escaped = false;
        for (; q < end; q++) {
            char c = *q;
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{') braces++;
            else if (c == '}') { if (--braces < 0) return false; }
            else if (c == '[') brackets++;
            else if (c == ']') { if (--brackets < 0) return false; }
            if (braces == 0 && brackets == 0) { q++; break; }
        }
        if (braces != 0 || brackets != 0) return false;
    } else {
        while (q < end && *q != ',' && *q != '}' && *q != ']' && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n') q++;
    }
    *valueBegin = p;
    *valueEnd = q;
    return q > p;
}

static bool PlutoJsonPathRange(const char *begin, const char *end, const char *path,
                               const char **valueBegin, const char **valueEnd) {
    const char *objectBegin = begin, *objectEnd = end, *part = path;
    while (*part) {
        const char *dot = strchr(part, '.');
        size_t length = dot ? (size_t)(dot - part) : strlen(part);
        char key[96];
        if (!length || length >= sizeof(key)) return false;
        memcpy(key, part, length); key[length] = 0;
        const char *vb = NULL, *ve = NULL;
        if (!PlutoJsonMemberRange(objectBegin, objectEnd, key, &vb, &ve)) return false;
        if (!dot) { *valueBegin = vb; *valueEnd = ve; return true; }
        if (*vb != '{' || ve <= vb + 1) return false;
        objectBegin = vb + 1; objectEnd = ve - 1; part = dot + 1;
    }
    return false;
}

static bool PlutoJsonNumberPath(const char *begin, const char *end, const char *path, double *out) {
    const char *vb = NULL, *ve = NULL;
    if (!PlutoJsonPathRange(begin, end, path, &vb, &ve) || vb >= ve) return false;
    char *tail = NULL;
    double value = strtod(vb, &tail);
    if (tail == vb || tail > ve) return false;
    *out = value;
    return true;
}

static bool PlutoJsonBoolPath(const char *begin, const char *end, const char *path, bool *out) {
    const char *vb = NULL, *ve = NULL;
    if (!PlutoJsonPathRange(begin, end, path, &vb, &ve)) return false;
    if (ve - vb == 4 && memcmp(vb, "true", 4) == 0) { *out = true; return true; }
    if (ve - vb == 5 && memcmp(vb, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool PlutoJsonStringPath(const char *begin, const char *end, const char *path, char *out, size_t cap) {
    const char *vb = NULL, *ve = NULL;
    if (!PlutoJsonPathRange(begin, end, path, &vb, &ve) || vb >= ve || *vb != '"' || cap == 0) return false;
    const char *p = vb + 1; size_t n = 0;
    while (p < ve && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') {
            if (p >= ve) return false;
            c = (unsigned char)*p++;
            if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
            else if (c == 'u') { if (ve - p < 4) return false; p += 4; c = '?'; }
        }
        if (n + 1 < cap) out[n++] = (char)c;
    }
    if (p >= ve || *p != '"') return false;
    out[n] = 0;
    return true;
}

static float PlutoReadNumber(const char *begin, const char *end, const char *path, float fallback) {
    double value;
    return PlutoJsonNumberPath(begin, end, path, &value) && isfinite(value) ? (float)value : fallback;
}

static int PlutoTrackIndex(const char *name) {
    static const char *names[] = {"v1", "v2", "v3", "v4", "v5", "v6", "a1", "a2", "a3", "a4", "a5", "a6"};
    for (int i = 0; i < TIMELINE_MAX_VIDEO_TRACKS + TIMELINE_MAX_AUDIO_TRACKS; i++)
        if (strcmp(name, names[i]) == 0) return i;
    return 0;
}

static const char *PlutoTrackName(int track) {
    static const char *names[] = {"v1", "v2", "v3", "v4", "v5", "v6", "a1", "a2", "a3", "a4", "a5", "a6"};
    if (track < 0) track = 0;
    if (track >= TIMELINE_MAX_VIDEO_TRACKS + TIMELINE_MAX_AUDIO_TRACKS) track = TIMELINE_MAX_VIDEO_TRACKS + TIMELINE_MAX_AUDIO_TRACKS - 1;
    return names[track];
}

static bool PlutoSyncTimeline(const char *json) {
    const char *array = strstr(json, "\"clips\":[");
    if (!array) return false;
    array += strlen("\"clips\":[");
    char selectedId[64] = {0};
    if (gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount)
        strncpy(selectedId, gTimelineClips[gTimelineSelected].id, sizeof(selectedId) - 1);
    int count = 0;
    const char *selectedBegin = NULL, *selectedEnd = NULL;
    const char *p = array;
    while (*p && *p != ']' && count < MAX_TIMELINE_CLIPS) {
        if (*p == ',' || *p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') { p++; continue; }
        if (*p != '{') break;
        const char *begin = p, *end = NULL;
        int depth = 0; bool inString = false, escaped = false;
        for (; *p; p++) {
            char c = *p;
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{') depth++;
            else if (c == '}' && --depth == 0) { end = p + 1; p++; break; }
        }
        if (!end) break;
        TimelineClipUi *clip = &gTimelineClips[count];
        memset(clip, 0, sizeof(*clip));
        clip->mediaIndex = -1;
        double start = 0.0, duration = 0.0;
        char track[16] = {0};
        if (PlutoJsonStringRange(begin, end, "id", clip->id, sizeof(clip->id)) &&
            PlutoJsonStringRange(begin, end, "label", clip->label, sizeof(clip->label))) {
            PlutoJsonStringRange(begin, end, "path", clip->path, sizeof(clip->path));
            PlutoJsonStringRange(begin, end, "track", track, sizeof(track));
            PlutoJsonStringRange(begin, end, "groupId", clip->groupId, sizeof(clip->groupId));
            PlutoJsonStringRange(begin, end, "linkedAudioId", clip->linkedId, sizeof(clip->linkedId));
            PlutoJsonStringRange(begin, end, "kind", clip->kind, sizeof(clip->kind));
            PlutoJsonStringPath(begin, end, "props.transitionOut.type", clip->transitionType, sizeof(clip->transitionType));
            PlutoJsonNumberRange(begin, end, "start", &start);
            PlutoJsonNumberRange(begin, end, "dur", &duration);
            clip->sourceOffset = PlutoReadNumber(begin, end, "sourceOffset", 0.f);
            clip->sourceDuration = PlutoReadNumber(begin, end, "sourceDuration", 0.f);
            clip->transitionDuration = PlutoReadNumber(begin, end, "props.transitionOut.duration", 1.f);
            clip->audioVolume = PlutoReadNumber(begin, end, "props.audio.volume", 100.f);
            clip->audioPan = PlutoReadNumber(begin, end, "props.audio.pan", 0.f);
            clip->audioFadeInDuration = PlutoReadNumber(begin, end, "props.audio.fadeInDuration", 1.f);
            clip->audioFadeOutDuration = PlutoReadNumber(begin, end, "props.audio.fadeOutDuration", 1.f);
            PlutoJsonBoolPath(begin, end, "props.audio.fadeIn", &clip->audioFadeIn);
            PlutoJsonBoolPath(begin, end, "props.audio.fadeOut", &clip->audioFadeOut);
            clip->track = PlutoTrackIndex(track);
            const char *hasAudio = strstr(begin, "\"hasAudio\":true");
            clip->hasAudio = hasAudio && hasAudio < end;
            for (int media = 0; media < gImportedCount; media++)
                if (clip->path[0] && strcmp(clip->path, gImported[media].path) == 0) {
                    clip->mediaIndex = media;
                    strncpy(clip->audioPath, gImported[media].audioPath, sizeof(clip->audioPath) - 1);
                    break;
                }
            clip->start = (float)start;
            clip->duration = (float)duration;
            const char *ka = strstr(begin, "\"keyframes\":[");
            if (ka && ka < end) {
                const char *kp = ka + strlen("\"keyframes\":[");
                while (kp < end && *kp != ']' && clip->keyframeCount < 32) {
                    if (*kp == ',' || *kp == ' ' || *kp == '\r' || *kp == '\n' || *kp == '\t') { kp++; continue; }
                    if (*kp != '{') break;
                    const char *kb = kp, *ke = NULL;
                    int kd = 0; bool ks = false, kesc = false;
                    for (; kp < end; kp++) {
                        char c = *kp;
                        if (ks) { if (kesc) kesc = false; else if (c == '\\') kesc = true; else if (c == '"') ks = false; continue; }
                        if (c == '"') ks = true;
                        else if (c == '{') kd++;
                        else if (c == '}' && --kd == 0) { ke = kp + 1; kp++; break; }
                    }
                    if (!ke) break;
                    double kt = 0.0;
                    if (PlutoJsonNumberRange(kb, ke, "time", &kt)) {
                        TimelineKeyframeUi *keyframe = &clip->keyframes[clip->keyframeCount++];
                        keyframe->time = (float)kt;
                        static const char *valuePaths[6] = {
                            "values.x", "values.y", "values.scaleW", "values.scaleH", "values.rotation", "values.opacity"
                        };
                        for (int field = 0; field < 6; field++) {
                            double value = 0.0;
                            if (PlutoJsonNumberPath(kb, ke, valuePaths[field], &value) && isfinite(value)) {
                                if (field == 5 && value > 0.0 && value <= 1.0) value *= 100.0;
                                keyframe->value[field] = (float)value;
                                keyframe->valueMask |= 1u << field;
                            }
                        }
                    }
                }
            }
            if (selectedId[0] && strcmp(selectedId, clip->id) == 0) { selectedBegin = begin; selectedEnd = end; }
            count++;
        }
    }
    gTimelineClipCount = count;
    gTimelineSelected = -1;
    for (int i = 0; i < count; i++) if (selectedId[0] && strcmp(selectedId, gTimelineClips[i].id) == 0) gTimelineSelected = i;
    if (gTimelineSelected >= 0 && selectedBegin && selectedEnd) PlutoHydrateInspector(selectedBegin, selectedEnd);
    else gTextTitleSelected = false;
    if (strstr(json, "\"snapping\":false")) gTimelineSnap = false;
    else if (strstr(json, "\"snapping\":true")) gTimelineSnap = true;
    return true;
}

static bool PlutoTimelineCall(const char *request) {
    if (!gBackend.request) { strcpy(gBackendMessage, "Rust backend DLL is unavailable"); return false; }
    char *response = gBackend.request(request);
    bool okay = response && strstr(response, "\"ok\":true") != NULL;
    if (okay) {
        PlutoSyncTimeline(response);
        gBackendMessage[0] = 0;
    } else if (response) {
        PlutoJsonString(response, "error", gBackendMessage, sizeof(gBackendMessage));
    }
    if (response) gBackend.release(response);
    return okay;
}

/* Live drag updates intentionally skip full timeline JSON serialization/parsing.
   The authoritative state changes synchronously in Rust; one full sync occurs on release. */
static bool PlutoTimelineCallFast(const char *request) {
    if (!gBackend.request) { strcpy(gBackendMessage, "Rust backend DLL is unavailable"); return false; }
    char *response = gBackend.request(request);
    bool okay = response && strstr(response, "\"ok\":true") != NULL;
    if (okay) gBackendMessage[0] = 0;
    else if (response) PlutoJsonString(response, "error", gBackendMessage, sizeof(gBackendMessage));
    if (response) gBackend.release(response);
    return okay;
}

static void PlutoTimelineSelect(int index) {
    if (index < 0 || index >= gTimelineClipCount || index == gTimelineSelected) return;
    /* Flush and close a text edit before inspector hydration replaces its global state. */
    PlutoTextAutoUpdateFlush();
    PlutoTimelineEndPropertyEdit();
    gTimelineSelected = index;
    gSelectedKeyframeClip = -1;
    gSelectedKeyframeTime = -1.f;
    PlutoTimelineCall("{\"command\":\"timeline_get\"}");
    gPreviewDirty = true;
}

static void PlutoJsonEscape(const char *src, char *dst, size_t cap) {
    size_t n = 0;
    if (!cap) return;
    while (*src && n + 7 < cap) {
        unsigned char c = (unsigned char)*src++;
        if (c == '"' || c == '\\') { dst[n++] = '\\'; dst[n++] = (char)c; }
        else if (c == '\n') { dst[n++] = '\\'; dst[n++] = 'n'; }
        else if (c == '\r') { dst[n++] = '\\'; dst[n++] = 'r'; }
        else if (c == '\t') { dst[n++] = '\\'; dst[n++] = 't'; }
        else if (c < 0x20) { n += (size_t)snprintf(dst + n, cap - n, "\\u%04x", c); }
        else dst[n++] = (char)c;
    }
    dst[n] = 0;
}

static bool PlutoTimelineAddMedia(int mediaIndex, float start, int track) {
    if (mediaIndex < 0 || mediaIndex >= gImportedCount || gTimelineClipCount >= MAX_TIMELINE_CLIPS) return false;
    ImportedMedia *media = &gImported[mediaIndex];
    char path[2048], label[520], id[64], request[4096];
    PlutoJsonEscape(media->path, path, sizeof(path));
    PlutoJsonEscape(media->name, label, sizeof(label));
    snprintf(id, sizeof(id), "clip-%llu", (unsigned long long)gClipSerial++);
    if ((strcmp(media->kind, "audio") == 0) != (track >= TIMELINE_MAX_VIDEO_TRACKS)) track = strcmp(media->kind, "audio") == 0 ? TIMELINE_MAX_VIDEO_TRACKS : 0;
    snprintf(request, sizeof(request),
             "{\"command\":\"timeline_add_clip\",\"clip\":{\"id\":\"%s\",\"track\":\"%s\",\"start\":%.4f,\"dur\":%.4f,\"label\":\"%s\",\"path\":\"%s\",\"kind\":\"%s\",\"hasAudio\":%s,\"sourceOffset\":0,\"sourceDuration\":%.4f,\"props\":{}}}",
             id, PlutoTrackName(track), fmaxf(0.f, start), fmaxf(.05f, media->duration), label, path,
             media->kind, media->hasAudio ? "true" : "false", media->duration);
    if (!PlutoTimelineCall(request)) return false;
    for (int i = 0; i < gTimelineClipCount; i++) if (strcmp(gTimelineClips[i].id, id) == 0) { PlutoTimelineSelect(i); break; }
    gPreviewDirty = true;
    return true;
}

static bool PlutoTimelineAddTitleClip(const char *path, const char *textJson, float start, float duration) {
    if (!path || !*path || !textJson || gTimelineClipCount >= MAX_TIMELINE_CLIPS) return false;
    char escapedPath[2048], id[64], request[8192];
    PlutoJsonEscape(path, escapedPath, sizeof(escapedPath));
    bool idInUse;
    do {
        snprintf(id, sizeof(id), "title-%llu", (unsigned long long)gClipSerial++);
        idInUse = false;
        for (int i = 0; i < gTimelineClipCount; i++) {
            if (strcmp(gTimelineClips[i].id, id) == 0) { idInUse = true; break; }
        }
    } while (idInUse);
    int n = snprintf(request, sizeof(request),
        "{\"command\":\"timeline_add_clip\",\"clip\":{\"id\":\"%s\",\"track\":\"v6\",\"start\":%.4f,\"dur\":%.4f,\"label\":\"Title\",\"path\":\"%s\",\"kind\":\"image\",\"hasAudio\":false,\"sourceOffset\":0,\"sourceDuration\":%.4f,\"props\":{\"text\":%s}}}",
        id, fmaxf(0.f, start), fmaxf(.05f, duration), escapedPath, fmaxf(.05f, duration), textJson);
    if (n <= 0 || (size_t)n >= sizeof(request) || !PlutoTimelineCall(request)) return false;
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (strcmp(gTimelineClips[i].id, id) == 0) { PlutoTimelineSelect(i); break; }
    }
    gRightTab = 3;
    gTextTitleSelected = true;
    gPreviewDirty = true;
    return true;
}

static bool PlutoTimelineUpdateTitleClip(const char *clipId, const char *path, const char *textJson, bool live) {
    if (!clipId || !*clipId || !path || !*path || !textJson) return false;
    char escapedId[160], escapedPath[2048], request[8192];
    PlutoJsonEscape(clipId, escapedId, sizeof(escapedId));
    PlutoJsonEscape(path, escapedPath, sizeof(escapedPath));
    int n = snprintf(request, sizeof(request),
        "{\"command\":\"timeline_update_title_clip\",\"id\":\"%s\",\"path\":\"%s\",\"text\":%s,\"live\":%s}",
        escapedId, escapedPath, textJson, live ? "true" : "false");
    if (n <= 0 || (size_t)n >= sizeof(request)) return false;
    bool okay;
    if (live) {
        gTimelinePropertyEditOpen = true;
        okay = PlutoTimelineCallFast(request);
    } else okay = PlutoTimelineCall(request);
    if (okay) {
        gTextTitleSelected = true;
        gRightTab = 3;
        gPreviewDirty = true;
        gPreviewDebounce = 0.f;
    }
    return okay;
}

static bool PlutoTimelineMove(int index, float start, int track) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    char id[128], request[512]; PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    float snapThreshold = fmaxf(.18f, fminf(.75f, 13.f / fmaxf(1.f, 21.5f * gTimelineZoom)));
    snprintf(request, sizeof(request), "{\"command\":\"timeline_move_clip\",\"id\":\"%s\",\"start\":%.4f,\"track\":\"%s\",\"playhead\":%.4f,\"snapThreshold\":%.4f}",
             id, fmaxf(0.f, start), PlutoTrackName(track), gPlayhead, snapThreshold);
    bool okay = PlutoTimelineCall(request);
    if (okay) gPreviewDirty = true;
    return okay;
}

static bool PlutoTimelineTrim(int index, float start, float end) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    char id[128], request[512]; PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    snprintf(request, sizeof(request),
             "{\"command\":\"timeline_trim_clip\",\"id\":\"%s\",\"start\":%.6f,\"end\":%.6f,\"sourceDuration\":%.6f}",
             id, fmaxf(0.f, start), end, gTimelineClips[index].sourceDuration);
    bool okay = PlutoTimelineCall(request);
    if (okay) gPreviewDirty = true;
    return okay;
}

static bool PlutoTimelineDelete(int index) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    char id[128], request[256]; PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    snprintf(request, sizeof(request), "{\"command\":\"timeline_delete_clip\",\"id\":\"%s\"}", id);
    bool okay = PlutoTimelineCall(request);
    if (okay) {
        gPreviewDirty = true;
        gSelectedKeyframeClip = -1;
        gSelectedKeyframeTime = -1.f;
    }
    return okay;
}

static bool PlutoTimelineGroupSelected(void) {
    char request[8192];
    int used = snprintf(request, sizeof(request), "{\"command\":\"timeline_group_clips\",\"groupId\":\"group-%llu\",\"ids\":[",
                        (unsigned long long)gClipSerial++);
    int selected = 0;
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (!gTimelineMultiSelect[i]) continue;
        char escaped[160]; PlutoJsonEscape(gTimelineClips[i].id, escaped, sizeof(escaped));
        int n = snprintf(request + used, sizeof(request) - (size_t)used, "%s\"%s\"", selected ? "," : "", escaped);
        if (n < 0 || (size_t)n >= sizeof(request) - (size_t)used) return false;
        used += n; selected++;
    }
    if (selected < 2 || (size_t)used + 3 >= sizeof(request)) return false;
    strcpy(request + used, "]}");
    return PlutoTimelineCall(request);
}

static bool PlutoTimelineUngroup(int index) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    char id[160], request[256]; PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    snprintf(request, sizeof(request), "{\"command\":\"timeline_ungroup_clip\",\"id\":\"%s\"}", id);
    return PlutoTimelineCall(request);
}

static bool PlutoTimelineSeparateAudio(int index) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    char id[160], request[512], audioId[64];
    PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    snprintf(audioId, sizeof(audioId), "audio-%llu", (unsigned long long)gClipSerial++);
    snprintf(request, sizeof(request), "{\"command\":\"timeline_separate_audio\",\"id\":\"%s\",\"audioId\":\"%s\"}", id, audioId);
    bool okay = PlutoTimelineCall(request);
    if (okay) gPreviewDirty = true;
    return okay;
}

static bool PlutoTimelineUnlink(int index) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    char id[160], request[256]; PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    snprintf(request, sizeof(request), "{\"command\":\"timeline_unlink_clip\",\"id\":\"%s\"}", id);
    return PlutoTimelineCall(request);
}

static bool PlutoTimelineSplit(int index, float at) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    char id[128], rightId[64], request[384];
    PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    snprintf(rightId, sizeof(rightId), "clip-%llu", (unsigned long long)gClipSerial++);
    snprintf(request, sizeof(request), "{\"command\":\"timeline_split_clip\",\"id\":\"%s\",\"at\":%.4f,\"rightId\":\"%s\"}", id, at, rightId);
    if (!PlutoTimelineCall(request)) return false;
    for (int i = 0; i < gTimelineClipCount; i++) if (strcmp(gTimelineClips[i].id, rightId) == 0) { gTimelineSelected = i; break; }
    gPreviewDirty = true;
    return true;
}

static bool PlutoTimelineSnap(bool enabled) {
    bool okay = PlutoTimelineCall(enabled ? "{\"command\":\"timeline_set_snapping\",\"enabled\":true}" : "{\"command\":\"timeline_set_snapping\",\"enabled\":false}");
    if (okay) gTimelineSnap = enabled;
    return okay;
}

static bool PlutoTimelineAddKeyframe(float at) {
    if (gTimelineSelected < 0 || gTimelineSelected >= gTimelineClipCount) return false;
    char id[128], request[768]; PlutoJsonEscape(gTimelineClips[gTimelineSelected].id, id, sizeof(id));
    snprintf(request, sizeof(request), "{\"command\":\"timeline_add_keyframe\",\"id\":\"%s\",\"at\":%.4f,\"values\":{\"x\":%.4f,\"y\":%.4f,\"scaleW\":%.4f,\"scaleH\":%.4f,\"rotation\":%.4f,\"opacity\":%.4f}}",
             id, at, gNum[0], gNum[1], gNum[2], gNum[3], gRotT, gSlT[0] * 100.f);
    bool okay = PlutoTimelineCall(request);
    if (okay) gPreviewDirty = true;
    return okay;
}

static bool PlutoTimelineDeleteKeyframe(int index, float at) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    char id[128], request[512];
    PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    snprintf(request, sizeof(request),
        "{\"command\":\"timeline_delete_keyframe\",\"id\":\"%s\",\"at\":%.6f}", id, at);
    PlutoTimelineEndPropertyEdit();
    bool okay = PlutoTimelineCall(request);
    if (okay) {
        gSelectedKeyframeClip = -1;
        gSelectedKeyframeTime = -1.f;
        gPreviewDirty = true;
        gPreviewDebounce = 0.f;
    }
    return okay;
}

static bool PlutoTimelineHistory(bool redo) {
    bool okay = PlutoTimelineCall(redo ? "{\"command\":\"timeline_redo\"}" : "{\"command\":\"timeline_undo\"}");
    if (okay) {
        gSelectedKeyframeClip = -1;
        gSelectedKeyframeTime = -1.f;
        /* Undo/redo returns the authoritative timeline; refresh the preview immediately. */
        gPreviewDirty = true;
        gPreviewDebounce = 0.f;
    }
    return okay;
}

static void PlutoPollBackend(void) {
    if (!gBackend.poll) return;
    for (int i = 0; i < 16; i++) {
        char *event = gBackend.poll();
        if (!event) break;
        if (strstr(event, "\"command\":\"preview_frame\"")) {
            double requestId = 0.0;
            StellaPreviewRequest completedRequest = {0};
            bool matchingRequest = PlutoJsonNumber(event, "requestId", &requestId) &&
                                   PlutoCompletePreviewRequest((uint64_t)requestId, &completedRequest);
            if (!matchingRequest && strstr(event, "\"sharedGl\":true") && gBackend.discardGlPreview && requestId > 0.0)
                gBackend.discardGlPreview((uint64_t)requestId);
            if (matchingRequest) {
                const float requestAt = completedRequest.at;
                const int requestRes = completedRequest.resolution;
                const int requestFps = completedRequest.fps > 0 ? completedRequest.fps : gPreviewFps;
                const double requestStartedAt = completedRequest.startedAt;
                bool previewMoving = gTimelinePlaying || gTimelineScrubbing || gPreviewNavigating;
                float previewLag = fmaxf(.2f, 6.f / (float)fmaxf(1, gPreviewFps));
                bool forward = gPreviewDisplayedAt < 0.f || gPlayhead >= gPreviewDisplayedAt;
                bool newerInDirection = gPreviewDisplayedAt < 0.f ||
                    (forward ? requestAt >= gPreviewDisplayedAt - .5f / gPreviewFps
                             : requestAt <= gPreviewDisplayedAt + .5f / gPreviewFps);
                bool movingFrameFresh = fabsf(requestAt - gPlayhead) <= previewLag && newerInDirection;
                bool currentRequest = requestRes == gPreviewRes && requestFps == gPreviewFps &&
                    (gTimelinePlaying ? newerInDirection : previewMoving ? movingFrameFresh : (!gPreviewDirty &&
                     fabsf(requestAt - gPlayhead) <= (0.5f / (float)fmaxf(1, requestFps))));
                if (strstr(event, "\"ok\":true")) {
                    double renderedAt = -1.0, renderedHeight = 0.0, frameIdNumber = 0.0;
                    bool frameMatchesRequest =
                        PlutoJsonNumber(event, "at", &renderedAt) &&
                        PlutoJsonNumber(event, "height", &renderedHeight) &&
                        PlutoJsonNumber(event, "frameId", &frameIdNumber) &&
                        fabs(renderedAt - requestAt) <= (0.5 / (double)fmaxf(1, requestFps)) &&
                        (int)renderedHeight == requestRes;
                    if (currentRequest && frameMatchesRequest) {
                        double uploadedAt = renderedAt;
                        double uploadStartedAt = GetTime();
                        bool upload = PlutoUploadPreviewFrame((uint64_t)frameIdNumber, &uploadedAt);
                        if (!upload) {
                            snprintf(gBackendMessage, sizeof(gBackendMessage), "Could not upload preview frame");
                            /* A failed shared-texture handoff may have disabled WGL
                               interop. Request another frame so the backend can use
                               its asynchronous CPU-readback compatibility path. */
                            gPreviewDirty = true;
                        } else {
                            gPreviewDisplayedAt = (float)uploadedAt;
                            gPerfSharedGl = strstr(event, "\"sharedGl\":true") != NULL;
                            /* F3 end-to-end latency must follow the frame actually
                               displayed, not whichever worker happened to finish last. */
                            gPreviewRequestAt = requestAt;
                            gPreviewRequestRes = requestRes;
                            gPreviewRequestStartedAt = requestStartedAt;
                            double now = GetTime();
                            {   /* Phase 1 stats: feed the F3 overlay on every displayed frame. */
                                double q = 0.0, su = 0.0, dc = 0.0, bk = 0.0;
                                PlutoJsonNumber(event, "queueMs", &q);
                                PlutoJsonNumber(event, "streamSetupMs", &su);
                                PlutoJsonNumber(event, "decodeWaitMs", &dc);
                                double rd = 0.0;
                                PlutoJsonNumber(event, "totalMs", &bk);
                                PlutoJsonNumber(event, "ringDepth", &rd);
                                PlutoJsonNumber(event, "hwDecode", &gPerfHwDecode);
                                PlutoJsonNumber(event, "path", &gPerfPath);
                                PlutoJsonNumber(event, "fallbackReason", &gPerfFallbackReason);
                                double xf = 0.0, cv = 0.0, cp = 0.0, rb = 0.0, pb = 0.0;
                                PlutoJsonNumber(event, "transferMs", &xf);
                                PlutoJsonNumber(event, "convertMs", &cv);
                                PlutoJsonNumber(event, "compositeMs", &cp);
                                PlutoJsonNumber(event, "readbackMs", &rb);
                                PlutoJsonNumber(event, "publishMs", &pb);
                                const double a = gPerfShownFrames ? 0.1 : 1.0;
                                gPerfTransferMs += (xf - gPerfTransferMs) * a;
                                gPerfConvertMs += (cv - gPerfConvertMs) * a;
                                gPerfCompositeMs += (cp - gPerfCompositeMs) * a;
                                gPerfReadbackMs += (rb - gPerfReadbackMs) * a;
                                gPerfPublishMs += (pb - gPerfPublishMs) * a;
                                gPerfQueueMs += (q - gPerfQueueMs) * a;
                                gPerfSetupMs += (su - gPerfSetupMs) * a;
                                gPerfDecodeMs += (dc - gPerfDecodeMs) * a;
                                gPerfBackendMs += (bk - gPerfBackendMs) * a;
                                gPerfRingDepth += (rd - gPerfRingDepth) * a;
                                gPerfUploadMs += ((now - uploadStartedAt) * 1000.0 - gPerfUploadMs) * a;
                                gPerfEndToEndMs += ((now - requestStartedAt) * 1000.0 - gPerfEndToEndMs) * a;
                                PerfNoteInterval(now);
                                gPerfLastShownAt = now;
                                gPerfShownFrames++; gPerfShownInWindow++;
                                if (now - gPerfFpsWindowAt >= 1.0) {
                                    gPerfDisplayFps = gPerfShownInWindow / (now - gPerfFpsWindowAt);
                                    gPerfShownInWindow = 0; gPerfFpsWindowAt = now;
                                }
                            }
                            if (previewMoving && now - gPreviewTelemetryAt >= 1.0) {
                                double queueMs = 0.0, setupMs = 0.0, decodeMs = 0.0, renderMs = 0.0;
                                PlutoJsonNumber(event, "queueMs", &queueMs);
                                PlutoJsonNumber(event, "streamSetupMs", &setupMs);
                                PlutoJsonNumber(event, "decodeWaitMs", &decodeMs);
                                PlutoJsonNumber(event, "totalMs", &renderMs);
                                char timing[384];
                                snprintf(timing, sizeof(timing),
                                    "[Stella preview] end-to-end=%.1fms queue=%.1fms setup=%.1fms decode=%.1fms (xfer=%.1f convert=%.1f) composite=%.1fms publish=%.1fms upload=%.1fms backend=%.1fms path=%s\n",
                                    (now - requestStartedAt) * 1000.0, queueMs, setupMs, decodeMs,
                                    gPerfTransferMs, gPerfConvertMs, gPerfCompositeMs, gPerfPublishMs,
                                    (now - uploadStartedAt) * 1000.0, renderMs,
                                    strstr(event, "\"sharedGl\":true") ? "WGPU-GL-ZERO-COPY" :
                                    (gPerfPath >= 1.5 ? "FFMPEG-FALLBACK" : "WGPU-readback"));
                                OutputDebugStringA(timing);
                                gPreviewTelemetryAt = now;
                            }
                        }
                        if (upload) {
                            /* Keep showing completed frames during playback/scrubbing even if
                               the playhead advanced slightly while FFmpeg rendered this one.
                               Reject older frames so the picture cannot visibly run backward. */
                            gPreviewDirty = previewMoving &&
                                fabsf(requestAt - gPlayhead) > (0.5f / (float)fmaxf(1, gPreviewFps));
                        }
                    } else {
                        if (strstr(event, "\"sharedGl\":true") && gBackend.discardGlPreview && frameIdNumber > 0.0)
                            gBackend.discardGlPreview((uint64_t)frameIdNumber);
                        gPerfDiscardedFrames++;
                        gPreviewDirty = true;
                    }
                } else if (strstr(event, "\"ok\":false")) {
                    if (currentRequest) {
                        PlutoJsonString(event, "error", gBackendMessage, sizeof(gBackendMessage));
                        gTimelinePlaying = false;
                        gPreviewDirty = false;
                    } else {
                        gPreviewDirty = true;
                    }
                }
            }
        } else if (strstr(event, "\"command\":\"export_project\"")) {
            if (strstr(event, "\"ok\":true")) {
                char path[1024] = {0};
                if (PlutoJsonString(event, "outputPath", path, sizeof(path))) snprintf(gBackendMessage, sizeof(gBackendMessage), "Export complete: %.180s", path);
            } else PlutoJsonString(event, "error", gBackendMessage, sizeof(gBackendMessage));
        } else if (strstr(event, "\"ok\":true")) {
            char path[1024], name[260], kind[16]; double duration = 0.0;
            if (PlutoJsonString(event, "path", path, sizeof(path)) &&
                PlutoJsonString(event, "name", name, sizeof(name)) &&
                PlutoJsonString(event, "kind", kind, sizeof(kind)) &&
                PlutoJsonNumber(event, "duration", &duration) && gImportedCount < MAX_IMPORTED_MEDIA) {
                ImportedMedia *asset = &gImported[gImportedCount++];
                strncpy(asset->path, path, sizeof(asset->path) - 1);
                strncpy(asset->name, name, sizeof(asset->name) - 1);
                strncpy(asset->kind, kind, sizeof(asset->kind) - 1);
                asset->duration = (float)duration;
                asset->hasAudio = strstr(event, "\"hasAudio\":true") != NULL;
                PlutoJsonString(event, "audioPlaybackPath", asset->audioPath, sizeof(asset->audioPath));
                if (PlutoJsonString(event, "waveformPath", asset->waveformPath, sizeof(asset->waveformPath))) {
                    asset->waveform = LoadTexture(asset->waveformPath);
                    if (asset->waveform.id) SetTextureFilter(asset->waveform, TEXTURE_FILTER_BILINEAR);
                }
                if (PlutoJsonString(event, "thumbnailPath", asset->thumbnailPath, sizeof(asset->thumbnailPath))) {
                    asset->thumbnail = LoadTexture(asset->thumbnailPath);
                    if (asset->thumbnail.id) SetTextureFilter(asset->thumbnail, TEXTURE_FILTER_BILINEAR);
                }
                gBackendMessage[0] = 0;
            }
        } else {
            PlutoJsonString(event, "error", gBackendMessage, sizeof(gBackendMessage));
        }
        gBackend.release(event);
    }
}

static void PlutoUnloadMedia(void) {
    for (int i = 0; i < gImportedCount; i++)
    {
        if (gImported[i].waveform.id) UnloadTexture(gImported[i].waveform);
        if (gImported[i].thumbnail.id) UnloadTexture(gImported[i].thumbnail);
    }
#ifdef _WIN32
    if (gPreviewFrameGlSlot) {
        PlutoQueueSharedPreviewRelease(gPreviewFrameGlSlot);
        gPreviewFrameGlSlot = 0;
        PlutoAfterDraw();
        gPreviewFrame = (Texture2D){0};
    } else if (gPreviewFrame.id) UnloadTexture(gPreviewFrame);
#else
    if (gPreviewFrame.id) UnloadTexture(gPreviewFrame);
#endif
    free(gPreviewPixels);
    gPreviewPixels = NULL;
    gPreviewPixelCapacity = 0;
    memset(gPreviewRequests, 0, sizeof(gPreviewRequests));
    gPreviewInFlightCount = 0;
    PlutoRefreshPreviewRequestMirrors();
}

static uint64_t PlutoRequestPreview(void) {
    if (!gBackend.submit || gPreviewInFlightCount >= STELLA_PREVIEW_REQUESTS) return 0;
    int requestSlot = -1;
    for (int i = 0; i < STELLA_PREVIEW_REQUESTS; i++)
        if (!gPreviewRequests[i].id) { requestSlot = i; break; }
    if (requestSlot < 0) return 0;
    char request[192];
    /* "playing" enables decode-ahead; "stepping" (arrow keys) makes the backend cache the
       frames before a seek target so repeated reverse steps do not each re-seek (Yoru). */
    snprintf(request, sizeof(request),
             "{\"command\":\"preview_frame\",\"at\":%.6f,\"height\":%d,\"fps\":%d,\"playing\":%s,\"stepping\":%s}",
             gPlayhead, gPreviewRes, gPreviewFps,
             gTimelinePlaying ? "true" : "false", gPreviewNavigating ? "true" : "false");
    uint64_t taskId = gBackend.submit(request);
    if (taskId) {
        StellaPreviewRequest *slot = &gPreviewRequests[requestSlot];
        memset(slot, 0, sizeof(*slot));
        slot->id = taskId;
        slot->at = gPlayhead;
        slot->resolution = gPreviewRes;
        slot->fps = gPreviewFps;
        slot->startedAt = GetTime();
        snprintf(slot->textPath, sizeof(slot->textPath), "%s", gTextOutputPath);
        gPreviewInFlightCount++;
        gPreviewRequestAt = gPlayhead;
        gPreviewRequestRes = gPreviewRes;
        gPreviewRequestStartedAt = slot->startedAt;
        PlutoRefreshPreviewRequestMirrors();
    }
    return taskId;
}

static void PlutoRequestExport(void) {
    if (!gBackend.submit) { strcpy(gBackendMessage, "Rust backend DLL is unavailable"); return; }
    OPENFILENAMEW dialog = {0};
    wchar_t path[32768] = L"stella-exported.mp4";
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetWindowHandle();
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    dialog.lpstrFilter = L"MP4 video\0*.mp4\0All files\0*.*\0\0";
    dialog.lpstrDefExt = L"mp4";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return;
    char *utf8 = (char *)malloc((size_t)bytes);
    if (!utf8) return;
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, bytes, NULL, NULL);
    char escaped[2048], request[2300];
    PlutoJsonEscape(utf8, escaped, sizeof(escaped));
    snprintf(request, sizeof(request), "{\"command\":\"export_project\",\"path\":\"%s\",\"width\":1920,\"height\":1080,\"fps\":30}", escaped);
    uint64_t task = gBackend.submit(request);
    snprintf(gBackendMessage, sizeof(gBackendMessage), task ? "Export queued" : "Could not queue export");
    free(utf8);
}

static bool PlutoTimelineSetPropertyJsonMode(const char *path, const char *valueJson, bool live) {
    if (gTimelineSelected < 0 || gTimelineSelected >= gTimelineClipCount) return false;
    live = live || gTimelinePropertyEditOpen;
    if (live) gTimelinePropertyEditOpen = true;
    char id[128], escapedPath[128], request[896];
    PlutoJsonEscape(gTimelineClips[gTimelineSelected].id, id, sizeof(id));
    PlutoJsonEscape(path, escapedPath, sizeof(escapedPath));
    snprintf(request, sizeof(request),
        "{\"command\":\"timeline_set_clip_property\",\"id\":\"%s\",\"path\":\"%s\",\"value\":%s,\"at\":%.6f,\"live\":%s}",
        id, escapedPath, valueJson, gPlayhead, live ? "true" : "false");
    bool ok = live ? PlutoTimelineCallFast(request) : PlutoTimelineCall(request);
    if (ok) gPreviewDirty = true;
    return ok;
}

static bool PlutoTimelineSetPropertyJson(const char *path, const char *valueJson) {
    return PlutoTimelineSetPropertyJsonMode(path, valueJson, false);
}

static bool PlutoTimelineSetPropertyLive(const char *path, double value) {
    char number[48]; snprintf(number, sizeof(number), "%.6f", value);
    return PlutoTimelineSetPropertyJsonMode(path, number, true);
}

static bool PlutoTimelineSetPropertiesJsonMode(const char *propertiesJson, bool live) {
    if (!propertiesJson || propertiesJson[0] != '[' || gTimelineSelected < 0 || gTimelineSelected >= gTimelineClipCount) return false;
    live = live || gTimelinePropertyEditOpen;
    if (live) gTimelinePropertyEditOpen = true;
    char id[128], request[8448];
    PlutoJsonEscape(gTimelineClips[gTimelineSelected].id, id, sizeof(id));
    int n = snprintf(request, sizeof(request),
        "{\"command\":\"timeline_set_clip_properties\",\"id\":\"%s\",\"properties\":%s,\"at\":%.6f,\"live\":%s}",
        id, propertiesJson, gPlayhead, live ? "true" : "false");
    if (n <= 0 || (size_t)n >= sizeof(request)) return false;
    bool ok = live ? PlutoTimelineCallFast(request) : PlutoTimelineCall(request);
    if (ok) gPreviewDirty = true;
    return ok;
}

static bool PlutoTimelineSetPropertiesJson(const char *propertiesJson) {
    return PlutoTimelineSetPropertiesJsonMode(propertiesJson, false);
}

static bool PlutoTimelineSetPropertiesLiveJson(const char *propertiesJson) {
    return PlutoTimelineSetPropertiesJsonMode(propertiesJson, true);
}

static void PlutoTimelineEndPropertyEdit(void) {
    if (!gTimelinePropertyEditOpen) return;
    /* A live title update has already requested the latest WGPU frame. Do not
       add the generic one-second settle delay when its undo group is committed. */
    const bool finishingTextEdit = gTextAutoUpdateCommitAt > 0.0;
    gTimelinePropertyEditOpen = false;
    gTextAutoUpdateCommitAt = 0.0;
    /* Commit one undo snapshot and refresh the local keyframe/inspector cache once. */
    gPreviewDirty = true;
    gPreviewDebounce = finishingTextEdit ? 0.f : 1.f;
    PlutoTimelineCall("{\"command\":\"timeline_end_property_edit\"}");
}

static bool PlutoTimelineMoveKeyframe(int index, float from, float to, bool live) {
    if (index < 0 || index >= gTimelineClipCount) return false;
    if (live) gTimelinePropertyEditOpen = true;
    char id[128], request[512];
    PlutoJsonEscape(gTimelineClips[index].id, id, sizeof(id));
    snprintf(request, sizeof(request),
        "{\"command\":\"timeline_move_keyframe\",\"id\":\"%s\",\"from\":%.6f,\"to\":%.6f,\"live\":%s}",
        id, from, to, live ? "true" : "false");
    bool okay = live ? PlutoTimelineCallFast(request) : PlutoTimelineCall(request);
    if (okay) {
        /* Keep the local timeline diamonds in sync without reparsing the full timeline. */
        if (live && fabsf(to - from) > 0.0005f) {
            TimelineClipUi *clip = &gTimelineClips[index];
            for (int k = 0; k < clip->keyframeCount; k++) {
                if (fabsf(clip->keyframes[k].time - from) <= 0.001f) {
                    clip->keyframes[k].time = to;
                    break;
                }
            }
            for (int a = 1; a < clip->keyframeCount; a++) {
                TimelineKeyframeUi value = clip->keyframes[a]; int b = a;
                while (b > 0 && clip->keyframes[b - 1].time > value.time) {
                    clip->keyframes[b] = clip->keyframes[b - 1]; b--;
                }
                clip->keyframes[b] = value;
            }
        }
        gPreviewDirty = true;
    }
    return okay;
}

static bool PlutoTimelineSetProperty(const char *path, double value) {
    char number[48]; snprintf(number, sizeof(number), "%.6f", value);
    return PlutoTimelineSetPropertyJson(path, number);
}

static bool PlutoTimelineSetBoolProperty(const char *path, bool value) {
    return PlutoTimelineSetPropertyJson(path, value ? "true" : "false");
}

static bool PlutoTimelineSetStringProperty(const char *path, const char *value) {
    char escaped[384], jsonValue[400];
    PlutoJsonEscape(value, escaped, sizeof(escaped));
    snprintf(jsonValue, sizeof(jsonValue), "\"%s\"", escaped);
    return PlutoTimelineSetPropertyJson(path, jsonValue);
}

static void PlutoRequestMediaImport(void) {
    if (!gBackend.submit) { strcpy(gBackendMessage, "Rust backend DLL is unavailable"); return; }
    OPENFILENAMEW dialog = {0};
    wchar_t path[32768] = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetWindowHandle();
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    dialog.lpstrFilter = L"Media files\0*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.mp3;*.wav;*.m4a;*.aac;*.flac;*.ogg;*.png;*.jpg;*.jpeg;*.webp;*.gif\0All files\0*.*\0\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return;
    char *utf8 = (char *)malloc((size_t)bytes);
    if (!utf8) return;
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, bytes, NULL, NULL);
    size_t cap = (size_t)bytes * 6 + 64;
    char *request = (char *)malloc(cap);
    if (request) {
        size_t n = (size_t)snprintf(request, cap, "{\"command\":\"inspect_media\",\"path\":\"");
        for (const unsigned char *p = (const unsigned char *)utf8; *p && n + 8 < cap; p++) {
            if (*p == '\\' || *p == '"') request[n++] = '\\';
            request[n++] = (char)*p;
        }
        n += (size_t)snprintf(request + n, cap - n, "\"}");
        if (!gBackend.submit(request)) strcpy(gBackendMessage, "Could not queue media inspection");
        free(request);
    }
    free(utf8);
}
#else
typedef struct { int unavailable; } PlutoBackend;
static PlutoBackend gBackend;
static bool PlutoBackendLoad(void) { return false; }
static bool PlutoTimelineCall(const char *request) { (void)request; return false; }
static void PlutoTimelineSelect(int index) { (void)index; }
static bool PlutoTimelineAddMedia(int mediaIndex, float start, int track) { (void)mediaIndex; (void)start; (void)track; return false; }
static bool PlutoTimelineAddTitleClip(const char *path, const char *textJson, float start, float duration) { (void)path; (void)textJson; (void)start; (void)duration; return false; }
static bool PlutoTimelineUpdateTitleClip(const char *clipId, const char *path, const char *textJson, bool live) { (void)clipId; (void)path; (void)textJson; (void)live; return false; }
static bool PlutoTimelineMove(int index, float start, int track) { (void)index; (void)start; (void)track; return false; }
static bool PlutoTimelineDelete(int index) { (void)index; return false; }
static bool PlutoTimelineSplit(int index, float at) { (void)index; (void)at; return false; }
static bool PlutoTimelineSnap(bool enabled) { (void)enabled; return false; }
static bool PlutoTimelineHistory(bool redo) { (void)redo; return false; }
static bool PlutoTimelineSetProperty(const char *path, double value) { (void)path; (void)value; return false; }
static bool PlutoTimelineSetPropertyLive(const char *path, double value) { (void)path; (void)value; return false; }
static bool PlutoTimelineSetPropertiesJson(const char *propertiesJson) { (void)propertiesJson; return false; }
static bool PlutoTimelineSetPropertiesLiveJson(const char *propertiesJson) { (void)propertiesJson; return false; }
static void PlutoTimelineEndPropertyEdit(void) { gTimelinePropertyEditOpen = false; gTextAutoUpdateCommitAt = 0.0; }
static bool PlutoTimelineMoveKeyframe(int index, float from, float to, bool live) { (void)index; (void)from; (void)to; (void)live; return false; }
static bool PlutoTimelineAddKeyframe(float at) { (void)at; return false; }
static bool PlutoTimelineDeleteKeyframe(int index, float at) { (void)index; (void)at; return false; }
static bool PlutoTimelineSetBoolProperty(const char *path, bool value) { (void)path; (void)value; return false; }
static bool PlutoTimelineSetStringProperty(const char *path, const char *value) { (void)path; (void)value; return false; }
static uint64_t PlutoRequestPreview(void) { return 0; }
static void PlutoRequestExport(void) {}
static void PlutoPollBackend(void) {}
static void PlutoUnloadMedia(void) {}
static void PlutoRequestMediaImport(void) {}
#endif

#endif
