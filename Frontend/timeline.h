/* Timeline panel. */
/* ------------------------------------------------------------------ */
/* Rust-backed timeline and editing controls                            */
/* ------------------------------------------------------------------ */
static float TimelinePps(void) { return 21.5f * gTimelineZoom; }
#define TIMELINE_LANE_MIN 56.f
#define TIMELINE_LANE_MAX 80.f
static float gTimelineLaneHeight = TIMELINE_LANE_MIN;

/* Track rows animate independently. A row remains in the Clay layout while it
   is fading out, so the rows below it do not jump when a clip is removed. */
static float gTimelineTrackVis[4] = {1.f, 0.f, 0.f, 0.f};
static bool gTimelineTrackWanted[4] = {true, false, false, false};
static int gTimelineDragTargetTrack = -1;
typedef struct TimelineSnapPoint { float time; int clipIndex; } TimelineSnapPoint;
static TimelineSnapPoint gTimelineSnapPoints[MAX_TIMELINE_CLIPS * 2];
static int gTimelineSnapPointCount;
static bool gTimelineSnapCacheValid;

static bool TimelineTrackIsVideo(int track) { return track >= 0 && track < TIMELINE_MAX_VIDEO_TRACKS; }
static bool TimelineTrackIsAudio(int track) { return track >= TIMELINE_MAX_VIDEO_TRACKS && track < TIMELINE_MAX_VIDEO_TRACKS + TIMELINE_MAX_AUDIO_TRACKS; }
static int TimelineTrackUnderPointer(void);
static int TimelineTrackUnderPointerForDrag(void);
static float gTimelineLastRevealY = -1.f;
static bool gTimelineWasDraggingVideo;
static float gTimelineLastAudioRevealY = -1.f;
static bool gTimelineWasDraggingAudio;

static bool TimelineVideoTrackIsVisible(int track) {
    for (int i = 0; i < gTimelineVideoTracks; i++)
        if (gTimelineVideoTrackIds[i] == track) return true;
    return false;
}

static void TimelineAddVisibleVideoTrack(int track) {
    if (track < 0 || track >= 4 || TimelineVideoTrackIsVisible(track) || gTimelineVideoTracks >= 4) return;
    /* Higher-numbered video layers sit above V1. Keeping the list in
       descending order also means a newly revealed layer is inserted at the
       top, exactly where an upward drag expects the next lane to appear. */
    int insert = gTimelineVideoTracks;
    while (insert > 0 && gTimelineVideoTrackIds[insert - 1] < track) {
        gTimelineVideoTrackIds[insert] = gTimelineVideoTrackIds[insert - 1];
        insert--;
    }
    gTimelineVideoTrackIds[insert] = track;
    gTimelineVideoTracks++;
}

static void TimelineRebuildVisibleVideoTracks(void) {
    /* V1 is permanent. Other video rows are wanted while occupied or while a
       drag is temporarily exposing them. Rows already fading out stay mounted
       until their visibility reaches zero. This avoids a one-frame layout pop. */
    bool wanted[4] = {true, false, false, false};
    for (int i = 0; i < gTimelineClipCount; i++) {
        int track = gTimelineClips[i].track;
        if (TimelineTrackIsVideo(track)) wanted[track] = true;
    }
    if (gTimelinePendingVideoTrack >= 1) {
        int last = gTimelinePendingVideoTrack < 3 ? gTimelinePendingVideoTrack : 3;
        for (int track = 1; track <= last; track++) wanted[track] = true;
    }
    for (int track = 0; track < 4; track++) gTimelineTrackWanted[track] = wanted[track];

    gTimelineVideoTracks = 0;
    for (int track = 3; track >= 0; track--) {
        if (wanted[track] || gTimelineTrackVis[track] > 0.001f)
            TimelineAddVisibleVideoTrack(track);
    }
}

static void TimelineUpdateTrackVisibility(void) {
    const float dt = fminf(fmaxf(gDt, 0.f), 0.05f);
    const float k = 1.f - expf(-24.f * dt);
    for (int track = 0; track < 4; track++) {
        float target = gTimelineTrackWanted[track] ? 1.f : 0.f;
        float delta = target - gTimelineTrackVis[track];
        if (fabsf(delta) < 0.002f) {
            gTimelineTrackVis[track] = target;
        } else {
            gTimelineTrackVis[track] += delta * k;
        }
    }
}

/* Audio lanes mirror the video rows, but grow downwards and stay completely
   hidden until an audio clip exists or an audio drag is entering the timeline. */
static void TimelineRebuildVisibleAudioTracks(void) {
    int highestWanted = -1;
    for (int i = 0; i < gTimelineClipCount; i++) {
        int track = gTimelineClips[i].track;
        if (TimelineTrackIsAudio(track)) {
            int lane = track - TIMELINE_MAX_VIDEO_TRACKS;
            if (lane > highestWanted) highestWanted = lane;
        }
    }
    if (gTimelinePendingAudioTrack >= TIMELINE_MAX_VIDEO_TRACKS) {
        int pending = gTimelinePendingAudioTrack - TIMELINE_MAX_VIDEO_TRACKS;
        if (pending >= TIMELINE_MAX_AUDIO_TRACKS) pending = TIMELINE_MAX_AUDIO_TRACKS - 1;
        if (pending > highestWanted) highestWanted = pending;
    }

    /* Audio lanes form a continuous stack: when A3 is occupied, A1 and A2
       remain available as empty lanes rather than leaving a visual hole. */
    bool wanted[TIMELINE_MAX_AUDIO_TRACKS] = {false};
    for (int i = 0; i <= highestWanted && i < TIMELINE_MAX_AUDIO_TRACKS; i++)
        wanted[i] = true;
    for (int i = 0; i < TIMELINE_MAX_AUDIO_TRACKS; i++)
        gTimelineAudioTrackWanted[i] = wanted[i];

    gTimelineAudioTracks = 0;
    for (int i = 0; i < TIMELINE_MAX_AUDIO_TRACKS; i++) {
        if (wanted[i] || gTimelineAudioTrackVis[i] > 0.001f)
            gTimelineAudioTracks = i + 1;
    }
}

static void TimelineUpdateAudioTrackVisibility(void) {
    const float dt = fminf(fmaxf(gDt, 0.f), 0.05f);
    const float k = 1.f - expf(-24.f * dt);
    for (int i = 0; i < TIMELINE_MAX_AUDIO_TRACKS; i++) {
        float target = gTimelineAudioTrackWanted[i] ? 1.f : 0.f;
        float delta = target - gTimelineAudioTrackVis[i];
        if (fabsf(delta) < 0.002f) gTimelineAudioTrackVis[i] = target;
        else gTimelineAudioTrackVis[i] += delta * k;
    }
}

static void TimelineExpandAudioTracksForDrag(void) {
    bool draggingAudio = false;
    int sourceTrack = -1;
    if (gClipDragIndex >= 0 && gClipDragIndex < gTimelineClipCount &&
        TimelineTrackIsAudio(gClipDragTrack)) {
        draggingAudio = true;
        sourceTrack = gClipDragTrack;
    } else if (gMediaDragIndex >= 0 && gMediaDragIndex < gImportedCount &&
               strcmp(gImported[gMediaDragIndex].kind, "audio") == 0) {
        draggingAudio = true;
    }

    bool draggingAny = gClipDragIndex >= 0 || gMediaDragIndex >= 0;
    if (!draggingAny || (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                         !IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
        gTimelineWasDraggingAudio = false;
        gTimelineLastAudioRevealY = -1.f;
        gTimelinePendingAudioTrack = -1;
        return;
    }
    if (!draggingAudio) {
        gTimelineWasDraggingAudio = false;
        gTimelineLastAudioRevealY = -1.f;
        return;
    }

    Vector2 mouse = GetMousePosition();
    Clay_BoundingBox viewport = Clay_GetElementData(CLAY_ID("TrackViewport")).boundingBox;
    bool inside = viewport.width > 0.f && CheckCollisionPointRec(mouse,
        (Rectangle){viewport.x, viewport.y, viewport.width, viewport.height});
    if (!gTimelineWasDraggingAudio) {
        gTimelineLastAudioRevealY = sourceTrack >= 0 ? mouse.y + 12.f
            : (viewport.y > 0.f ? viewport.y : mouse.y);
    }
    gTimelineWasDraggingAudio = true;

    /* Reveal A1 when an audio asset first enters the track viewport. It becomes
       a real drop target immediately, even while its row is animating in. */
    if (inside && gTimelinePendingAudioTrack < TIMELINE_MAX_VIDEO_TRACKS)
        gTimelinePendingAudioTrack = TIMELINE_MAX_VIDEO_TRACKS;

    int hovered = TimelineTrackUnderPointerForDrag();
    if (TimelineTrackIsAudio(hovered) && hovered > gTimelinePendingAudioTrack)
        gTimelinePendingAudioTrack = hovered;

    /* Audio layers expand downward one row at a time. The user can drag below
       the last visible row to reveal the next lane, without opening all six. */
    if (inside && gTimelineAudioTracks > 0 && gTimelineAudioTracks < TIMELINE_MAX_AUDIO_TRACKS) {
        int lastTrack = TIMELINE_MAX_VIDEO_TRACKS + gTimelineAudioTracks - 1;
        Clay_BoundingBox last = Clay_GetElementData(CLAY_IDI("TrackLane", lastTrack)).boundingBox;
        if (last.height > 0.f && mouse.y >= last.y + last.height - 5.f &&
            mouse.y <= last.y + last.height + 34.f && mouse.y >= gTimelineLastAudioRevealY) {
            int next = lastTrack + 1;
            if (next > gTimelinePendingAudioTrack) gTimelinePendingAudioTrack = next;
            gTimelineLastAudioRevealY = mouse.y + 12.f;
        }
    }
}

static void TimelineExpandVideoTracksForDrag(void) {
    bool draggingVideo = false;
    int sourceTrack = -1;
    if (gClipDragIndex >= 0 && gClipDragIndex < gTimelineClipCount &&
        TimelineTrackIsVideo(gClipDragTrack)) {
        draggingVideo = true;
        sourceTrack = gClipDragTrack;
    } else if (gMediaDragIndex >= 0 && gMediaDragIndex < gImportedCount &&
               strcmp(gImported[gMediaDragIndex].kind, "audio") != 0) {
        draggingVideo = true;
    }
    bool draggingAny = draggingVideo ||
        (gClipDragIndex >= 0 && gClipDragIndex < gTimelineClipCount) ||
        (gMediaDragIndex >= 0 && gMediaDragIndex < gImportedCount);
    if (!draggingAny || (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                         !IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
        gTimelineWasDraggingVideo = false;
        gTimelineLastRevealY = -1.f;
        gTimelineDragTargetTrack = -1;
        return;
    }
    if (!draggingVideo) {
        /* Audio drags use the same stable lane targeting, but do not reveal
           video rows. The target is refreshed by the drag hit-test below. */
        gTimelineWasDraggingVideo = false;
        gTimelineLastRevealY = -1.f;
        return;
    }

    Vector2 mouse = GetMousePosition();
    if (!gTimelineWasDraggingVideo) {
        int highestAtStart = 0;
        for (int i = 0; i < gTimelineVideoTracks; i++) {
            int track = gTimelineVideoTrackIds[i];
            if ((gTimelineTrackWanted[track] || gTimelineTrackVis[track] >= 0.18f) && track > highestAtStart)
                highestAtStart = track;
        }
        Clay_BoundingBox top = Clay_GetElementData(CLAY_IDI("TrackLane", highestAtStart)).boundingBox;
        /* A clip drag starts inside a lane, so require a deliberate 12px
           upward movement from the grab point. A media-library drag starts
           outside the timeline, so use the visible top lane as its threshold. */
        gTimelineLastRevealY = sourceTrack >= 0 ? mouse.y - 12.f
            : (top.height > 0.f ? top.y + 15.f : mouse.y);
    }
    gTimelineWasDraggingVideo = true;

    int hovered = TimelineTrackUnderPointerForDrag();
    if (TimelineTrackIsVideo(hovered)) {
        /* Reveal skipped tracks between the source lane and the lane under
           the pointer. During the drag those intermediate rows stay mounted;
           after release only tracks containing media remain visible. */
        int first = sourceTrack >= 0 ? (sourceTrack < hovered ? sourceTrack : hovered) : 0;
        int last = sourceTrack >= 0 ? (sourceTrack > hovered ? sourceTrack : hovered) : hovered;
        for (int track = first; track <= last; track++) {
            if (track > gTimelinePendingVideoTrack) gTimelinePendingVideoTrack = track;
            TimelineAddVisibleVideoTrack(track);
        }
    }

    /* One additional lane appears for each deliberate upward movement. The
       pointer can travel just above the track viewport into the ruler area,
       so each new lane requires another small upward step instead of all
       four lanes popping open at once. */
    int highest = -1;
    for (int i = 0; i < gTimelineVideoTracks; i++) {
        int track = gTimelineVideoTrackIds[i];
        if ((gTimelineTrackWanted[track] || gTimelineTrackVis[track] >= 0.18f) && track > highest)
            highest = track;
    }
    if (highest >= 0 && highest < 3) {
        Clay_BoundingBox top = Clay_GetElementData(CLAY_IDI("TrackLane", highest)).boundingBox;
        if (top.height > 0.f && mouse.y <= gTimelineLastRevealY &&
            mouse.y >= top.y - 48.f) {
            if (highest + 1 > gTimelinePendingVideoTrack)
                gTimelinePendingVideoTrack = highest + 1;
            TimelineAddVisibleVideoTrack(highest + 1);
            gTimelineLastRevealY = mouse.y - 12.f;
        }
    }
}

static int TimelineTrackUnderPointer(void) {
    Vector2 mouse = GetMousePosition();
    Clay_BoundingBox viewport = Clay_GetElementData(CLAY_ID("TrackViewport")).boundingBox;
    if (viewport.width > 0.f && !CheckCollisionPointRec(mouse,
        (Rectangle){viewport.x, viewport.y, viewport.width, viewport.height})) return -1;
    float best = INFINITY;
    int bestTrack = -1;
    for (int i = 0; i < gTimelineVideoTracks; i++) {
        int track = gTimelineVideoTrackIds[i];
        Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", track)).boundingBox;
        if (lane.width <= 0.f || lane.height < 10.f) continue;
        float center = lane.y + lane.height * .5f;
        float distance = fabsf(mouse.y - center);
        if (mouse.x >= lane.x - 4.f && mouse.x <= lane.x + lane.width + 4.f &&
            distance <= lane.height * .5f + fmaxf(6.f, lane.height * .18f) && distance < best) {
            best = distance; bestTrack = track;
        }
    }
    for (int track = 4; track < 4 + gTimelineAudioTracks; track++) {
        Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", track)).boundingBox;
        if (lane.width <= 0.f || lane.height < 10.f) continue;
        float center = lane.y + lane.height * .5f;
        float distance = fabsf(mouse.y - center);
        if (mouse.x >= lane.x - 4.f && mouse.x <= lane.x + lane.width + 4.f &&
            distance <= lane.height * .5f + fmaxf(6.f, lane.height * .18f) && distance < best) {
            best = distance; bestTrack = track;
        }
    }
    return bestTrack;
}

/* Keep the currently targeted row until the pointer has clearly crossed its
   edge. This is the small hysteresis that stops a dragged clip/media item from
   flipping between adjacent tracks while the pointer rests on their boundary. */
static int TimelineTrackUnderPointerForDrag(void) {
    /* Outside an active drag this is an ordinary hover query; do not let idle
       pointer movement seed a stale preferred lane for the next drag. */
    if (gClipDragIndex < 0 && gMediaDragIndex < 0) return TimelineTrackUnderPointer();
    Vector2 mouse = GetMousePosition();
    Clay_BoundingBox viewport = Clay_GetElementData(CLAY_ID("TrackViewport")).boundingBox;
    if (viewport.width > 0.f && !CheckCollisionPointRec(mouse,
        (Rectangle){viewport.x, viewport.y, viewport.width, viewport.height})) return -1;
    bool draggingAudio = (gClipDragIndex >= 0 && gClipDragIndex < gTimelineClipCount &&
                          TimelineTrackIsAudio(gClipDragTrack)) ||
                         (gMediaDragIndex >= 0 && gMediaDragIndex < gImportedCount &&
                          strcmp(gImported[gMediaDragIndex].kind, "audio") == 0);
    bool draggingVideo = (gClipDragIndex >= 0 && gClipDragIndex < gTimelineClipCount &&
                          TimelineTrackIsVideo(gClipDragTrack)) ||
                         (gMediaDragIndex >= 0 && gMediaDragIndex < gImportedCount &&
                          strcmp(gImported[gMediaDragIndex].kind, "audio") != 0);
    int preferred = gTimelineDragTargetTrack;
    if (preferred >= 0 && preferred < TIMELINE_MAX_VIDEO_TRACKS + TIMELINE_MAX_AUDIO_TRACKS &&
        ((!draggingAudio || TimelineTrackIsAudio(preferred)) &&
         (!draggingVideo || TimelineTrackIsVideo(preferred)))) {
        Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", preferred)).boundingBox;
        if (lane.width > 0.f && lane.height >= 10.f &&
            mouse.x >= lane.x - 4.f && mouse.x <= lane.x + lane.width + 4.f &&
            mouse.y >= lane.y - 4.f && mouse.y <= lane.y + lane.height + 4.f)
            return preferred;
    }
    int next = TimelineTrackUnderPointer();
    /* Keep the media kind on its own family of lanes. In particular, an audio
       asset dropped over V1 should reveal and target A1 instead of being
       silently rejected because the audio row was hidden at drag start. */
    if (draggingAudio && TimelineTrackIsVideo(next))
        next = (gClipDragIndex >= 0 && TimelineTrackIsAudio(gClipDragTrack))
            ? gClipDragTrack : TIMELINE_MAX_VIDEO_TRACKS;
    else if (draggingVideo && TimelineTrackIsAudio(next))
        next = (gClipDragIndex >= 0 && TimelineTrackIsVideo(gClipDragTrack))
            ? gClipDragTrack : 0;
    else if (next < 0 && draggingAudio) {
        Clay_BoundingBox audioViewport = Clay_GetElementData(CLAY_ID("TrackViewport")).boundingBox;
        if (audioViewport.width > 0.f && CheckCollisionPointRec(mouse,
            (Rectangle){audioViewport.x, audioViewport.y, audioViewport.width, audioViewport.height}))
            next = (gClipDragIndex >= 0 && TimelineTrackIsAudio(gClipDragTrack))
                ? gClipDragTrack : TIMELINE_MAX_VIDEO_TRACKS;
    }
    if (next >= 0) gTimelineDragTargetTrack = next;
    return next;
}

static void TimelineBuildSnapCache(void) {
    gTimelineSnapPointCount = 0;
    for (int i = 0; i < gTimelineClipCount && gTimelineSnapPointCount + 1 < MAX_TIMELINE_CLIPS * 2; i++) {
        if (!TimelineTrackIsVideo(gTimelineClips[i].track)) continue;
        gTimelineSnapPoints[gTimelineSnapPointCount++] = (TimelineSnapPoint){gTimelineClips[i].start, i};
        gTimelineSnapPoints[gTimelineSnapPointCount++] = (TimelineSnapPoint){gTimelineClips[i].start + gTimelineClips[i].duration, i};
    }
    gTimelineSnapCacheValid = true;
}

static void TimelinePrepareSnapCache(void) {
    if (gClipDragIndex < 0 && gMediaDragIndex < 0) {
        gTimelineSnapCacheValid = false;
    } else if (!gTimelineSnapCacheValid) {
        TimelineBuildSnapCache();
    }
}

static float TimelineSnapStart(float start, float duration, bool video, int movingIndex, float playhead, bool snapPlayhead, const char *excludeGroup) {
    if (!video || !gTimelineSnap) return fmaxf(0.f, start);

    /* The magnet is proximity snapping only. The desired mouse position remains
       the anchor; do not silently lock to the previous clip edge when snapping
       is disabled. Dragging uses a prebuilt edge cache instead of rescanning all
       clips on every pointer event. */
    float bestDistance = 0.25f, snapped = fmaxf(0.f, start);
    if (gClipDragIndex >= 0 || gMediaDragIndex >= 0) {
        if (!gTimelineSnapCacheValid) TimelineBuildSnapCache();
        for (int i = 0; i < gTimelineSnapPointCount; i++) {
            TimelineSnapPoint point = gTimelineSnapPoints[i];
            if (point.clipIndex == movingIndex ||
                (excludeGroup && *excludeGroup &&
                 strcmp(gTimelineClips[point.clipIndex].groupId, excludeGroup) == 0)) continue;
            for (int edge = 0; edge < 2; edge++) {
                float value = point.time - (edge ? duration : 0.f);
                float distance = fabsf(value - start);
                if (distance < bestDistance) { bestDistance = distance; snapped = fmaxf(0.f, value); }
            }
        }
        if (snapPlayhead) {
            for (int edge = 0; edge < 2; edge++) {
                float value = playhead - (edge ? duration : 0.f);
                float distance = fabsf(value - start);
                if (distance < bestDistance) { bestDistance = distance; snapped = fmaxf(0.f, value); }
            }
        }
        return snapped;
    }

    /* Non-drag edits keep the uncached path so it always reflects the latest
       project state. */
    for (int n = -1; n < gTimelineClipCount; n++) {
        if (n == movingIndex || (n >= 0 && (!TimelineTrackIsVideo(gTimelineClips[n].track) ||
            (excludeGroup && *excludeGroup && strcmp(gTimelineClips[n].groupId, excludeGroup) == 0)))) continue;
        float candidates[2]; int count = 0;
        if (n < 0) { if (snapPlayhead) candidates[count++] = playhead; }
        else { candidates[count++] = gTimelineClips[n].start; candidates[count++] = gTimelineClips[n].start + gTimelineClips[n].duration; }
        for (int c = 0; c < count; c++) for (int edge = 0; edge < 2; edge++) {
            float value = candidates[c] - (edge ? duration : 0.f);
            float distance = fabsf(value - start);
            if (distance < bestDistance) { bestDistance = distance; snapped = fmaxf(0.f, value); }
        }
    }
    return snapped;
}

static bool TimelineTrackOverlaps(int track, float start, float duration, int excluded, const char *excludeGroup) {
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (i == excluded || (excludeGroup && *excludeGroup && strcmp(gTimelineClips[i].groupId, excludeGroup) == 0) ||
            gTimelineClips[i].track != track) continue;
        if (start < gTimelineClips[i].start + gTimelineClips[i].duration - .0001f &&
            start + duration > gTimelineClips[i].start + .0001f) return true;
    }
    return false;
}

static int TimelineClosestFreeTrack(int requested, float start, float duration, int excluded, const char *excludeGroup) {
    int first = TimelineTrackIsVideo(requested) ? 0 : 4;
    int last = TimelineTrackIsVideo(requested) ? TIMELINE_MAX_VIDEO_TRACKS - 1
                                               : TIMELINE_MAX_VIDEO_TRACKS + TIMELINE_MAX_AUDIO_TRACKS - 1;
    int best = requested, bestDistance = 99;
    for (int track = first; track <= last; track++) {
        int distance = abs(track - requested);
        bool tiePrefersHigherVideoLane = distance == bestDistance && TimelineTrackIsVideo(track) && track > best;
        if ((distance < bestDistance || tiePrefersHigherVideoLane) && !TimelineTrackOverlaps(track, start, duration, excluded, excludeGroup)) {
            best = track; bestDistance = distance;
        }
    }
    return best;
}

static const char *TimelineTrackLabel(int track) {
    static const char *labels[] = {"V1", "V2", "V3", "V4", "A1", "A2", "A3", "A4", "A5", "A6"};
    int last = TIMELINE_MAX_VIDEO_TRACKS + TIMELINE_MAX_AUDIO_TRACKS - 1;
    return labels[track < 0 ? 0 : track > last ? last : track];
}

static void TimelineClipLane(int track, float height);

static void TimelineTrackRow(int track, float height) {
    Clay_ElementId row = CLAY_IDI("TrackRow", track);
    static const Color videoBands[3] = {{26, 27, 30, 255}, {25, 26, 29, 255}, {30, 30, 32, 255}};
    static const Color audioBands[2] = {{23, 24, 27, 255}, {26, 27, 30, 255}};
    Color bgRaylib = track < 4 ? videoBands[track % 3] : audioBands[(track - 4) % 2];
    /* FIX: Clay's backgroundColor is a Clay_Color (floats), not a raylib Color (unsigned char). */
    Clay_Color bg = (Clay_Color){(float)bgRaylib.r, (float)bgRaylib.g, (float)bgRaylib.b, (float)bgRaylib.a};
    CLAY(row, {.layout = {.sizing = {GROW, FIX(height)}, .childAlignment = AL_Y}, .backgroundColor = bg,
               .clip = {.horizontal = true, .vertical = true},
               .border = {.color = RGB(41, 42, 46), .width = {.bottom = 1}}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
            Icon(track < 4 ? I_FILM : I_NOTE, 14, C_MUTED);
            TxMedium(TimelineTrackLabel(track), 13, C_MUTED);
        }
        TimelineClipLane(track, height);
    }
}

static float TimelineDuration(void) {
    float duration = 0.f;
    for (int i = 0; i < gTimelineClipCount; i++)
        duration = fmaxf(duration, gTimelineClips[i].start + gTimelineClips[i].duration);
    return duration > 0.f ? duration : 9.f;
}

static void TimelineShiftUiKeyframes(TimelineClipUi *clip, float delta) {
    if (!clip || fabsf(delta) <= 0.00001f) return;
    for (int k = 0; k < clip->keyframeCount; k++) clip->keyframes[k].time += delta;
}

static void TimelineUpdateTrimDrag(void) {
    if (gClipTrimIndex < 0 || gClipTrimIndex >= gTimelineClipCount) return;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        /* Recover cleanly if the window lost mouse capture mid-gesture. */
        PlutoTimelineCall("{\"command\":\"timeline_get\"}");
        gClipTrimIndex = -1;
        return;
    }

    int index = gClipTrimIndex;
    TimelineClipUi *clip = &gTimelineClips[index];
    Vector2 mouse = GetMousePosition();
    float delta = (mouse.x - gClipTrimMouseX) / fmaxf(1.f, gClipTrimPixelsPerSecond);
    float nextStart = gClipTrimOrigStart;
    float nextEnd = gClipTrimOrigEnd;

    if (gClipTrimEdge < 0) {
        float minimumStart = fmaxf(0.f, gClipTrimOrigStart - gClipTrimOrigOffset);
        /* Expanding the left edge restores source frames, but never overlaps
           the previous clip on this lane. */
        for (int j = 0; j < gTimelineClipCount; j++) {
            if (j == index || gTimelineClips[j].track != clip->track) continue;
            if (clip->groupId[0] && strcmp(gTimelineClips[j].groupId, clip->groupId) == 0) continue;
            float otherEnd = gTimelineClips[j].start + gTimelineClips[j].duration;
            if (otherEnd <= gClipTrimOrigStart + 0.001f && otherEnd > minimumStart)
                minimumStart = otherEnd;
        }
        nextStart = fmaxf(minimumStart,
                          fminf(gClipTrimOrigEnd - 0.05f, gClipTrimOrigStart + delta));
    } else {
        float maximumEnd = gClipTrimSourceDuration > 0.f
            ? gClipTrimOrigStart + fmaxf(0.f, gClipTrimSourceDuration - gClipTrimOrigOffset)
            : gClipTrimOrigEnd + 3600.f;
        /* Expanding the right edge restores source frames, but never overlaps
           the next clip on this lane. */
        for (int j = 0; j < gTimelineClipCount; j++) {
            if (j == index || gTimelineClips[j].track != clip->track) continue;
            if (clip->groupId[0] && strcmp(gTimelineClips[j].groupId, clip->groupId) == 0) continue;
            if (gTimelineClips[j].start >= gClipTrimOrigEnd - 0.001f &&
                gTimelineClips[j].start < maximumEnd)
                maximumEnd = gTimelineClips[j].start;
        }
        nextEnd = fmaxf(gClipTrimOrigStart + 0.05f,
                        fminf(maximumEnd, gClipTrimOrigEnd + delta));
    }

    float previousStart = clip->start;
    TimelineShiftUiKeyframes(clip, nextStart - previousStart);
    clip->start = nextStart;
    clip->duration = fmaxf(0.05f, nextEnd - nextStart);
    clip->sourceOffset = fmaxf(0.f, gClipTrimOrigOffset + nextStart - gClipTrimOrigStart);

    /* The Rust timeline trims linked picture/audio clips as one operation.
       Mirror that relationship during the local drag preview as well. */
    if (clip->linkedId[0]) {
        for (int j = 0; j < gTimelineClipCount; j++) {
            TimelineClipUi *linked = &gTimelineClips[j];
            if (j == index || strcmp(linked->id, clip->linkedId) != 0) continue;
            TimelineShiftUiKeyframes(linked, nextStart - linked->start);
            linked->start = nextStart;
            linked->duration = clip->duration;
            linked->sourceOffset = clip->sourceOffset;
            break;
        }
    }

    gPreviewDirty = true;
    gPreviewDebounce = 0.f;
    gResize = 1;

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        bool changed = fabsf(nextStart - gClipTrimOrigStart) > 0.0005f ||
                       fabsf(nextEnd - gClipTrimOrigEnd) > 0.0005f;
        if (changed && !PlutoTimelineTrim(index, nextStart, nextEnd))
            PlutoTimelineCall("{\"command\":\"timeline_get\"}");
        gClipTrimIndex = -1;
    }
}

static int TimelineKeyframeHit(int clipIndex, Clay_BoundingBox bounds, Vector2 mouse) {
    if (clipIndex < 0 || clipIndex >= gTimelineClipCount || bounds.width <= 0.f) return -1;
    TimelineClipUi *clip = &gTimelineClips[clipIndex];
    for (int k = 0; k < clip->keyframeCount; k++) {
        float time = clip->keyframes[k].time;
        if (time < clip->start - .001f || time > clip->start + clip->duration + .001f) continue;
        float x = bounds.x + (time - clip->start) / fmaxf(.001f, clip->duration) * bounds.width;
        float y = bounds.y + 14.f;
        if (fabsf(mouse.x - x) <= 13.f && fabsf(mouse.y - y) <= 15.f) return k;
    }
    return -1;
}

static int TimelineKeyframeAtTime(int clipIndex, float time, float tolerance) {
    if (clipIndex < 0 || clipIndex >= gTimelineClipCount) return -1;
    TimelineClipUi *clip = &gTimelineClips[clipIndex];
    int best = -1;
    float bestDistance = tolerance;
    for (int k = 0; k < clip->keyframeCount; k++) {
        float distance = fabsf(clip->keyframes[k].time - time);
        if (distance <= bestDistance) { best = k; bestDistance = distance; }
    }
    return best;
}

static int TimelineKeyframeAtPlayhead(int clipIndex) {
    float tolerance = .5f / (float)fmaxf(1, gPreviewFps) + .001f;
    return TimelineKeyframeAtTime(clipIndex, gPlayhead, tolerance);
}

static int TimelineSelectedKeyframeIndex(int clipIndex) {
    if (gSelectedKeyframeClip != clipIndex || gSelectedKeyframeTime < 0.f) return -1;
    return TimelineKeyframeAtTime(clipIndex, gSelectedKeyframeTime, .0015f);
}

static bool TimelineKeyframeToolbarButton(Clay_ElementId id, int icon, bool active, bool accent, float size) {
    Bs b = Interact(id);
    Clay_Color base = active ? RGB(55, 45, 70) : RGB(28, 29, 34);
    Clay_Color hover = active ? RGB(76, 62, 96) : RGB(45, 47, 54);
    Clay_Color iconColor = accent ? (active ? RGB(225, 207, 255) : RGB(183, 164, 211))
                                : RGB(166, 169, 179);
    CLAY(id, {.layout = {.sizing = {FIX(size), FIX(30)}, .childAlignment = AL_C},
              .backgroundColor = BtnBg(base, hover, b),
              .cornerRadius = CLAY_CORNER_RADIUS(8),
              .border = {.color = Mix(RGB(45, 46, 53), accent ? RGB(126, 99, 164) : RGB(83, 86, 96),
                                      (active ? .72f : 0.f) + b.hover * .22f),
                         .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Icon(icon, 15.5f, iconColor);
    }
    return b.clicked;
}

static bool TimelineKeyframeAtPointer(Vector2 mouse) {
    for (int i = 0; i < gTimelineClipCount; i++) {
        Clay_BoundingBox bounds = Clay_GetElementData(CLAY_IDI("TimelineClip", i)).boundingBox;
        if (TimelineKeyframeHit(i, bounds, mouse) >= 0) return true;
    }
    return false;
}

static int TimelineClipStartCompare(const void *left, const void *right) {
    int a = *(const int *)left, b = *(const int *)right;
    float delta = gTimelineClips[a].start - gTimelineClips[b].start;
    if (delta < -0.000001f) return -1;
    if (delta > 0.000001f) return 1;
    return a < b ? -1 : a > b ? 1 : 0;
}

static void TimelineClipLane(int track, float height) {
    Clay_ElementId laneId = CLAY_IDI("TrackLane", track);
    Clay_BoundingBox lane = Clay_GetElementData(laneId).boundingBox;
    /* Collapsing/brand-new rows can be only a few pixels tall. Keep their Clay
       node alive for layout animation, but defer clip sorting and hit targets
       until the row is large enough to interact with. */
    if (lane.height < 10.f) {
        CLAY(laneId, {.layout = {.sizing = {GROW, FIX(height)}, .childGap = 0},
                      .clip = {.horizontal = true, .vertical = true}}) {}
        return;
    }
    float pps = TimelinePps();
    if (gTimelineTool == 2 && Clay_PointerOver(laneId)) gCross = true;
    bool transitionPressed = false;
    int order[MAX_TIMELINE_CLIPS], count = 0;
    for (int i = 0; i < gTimelineClipCount; i++) {
        if (gTimelineClips[i].track == track) order[count++] = i;
    }
    /* qsort avoids the old insertion-sort O(n^2) cost when large timelines
       are edited or redrawn. The index tie-break keeps ordering deterministic. */
    if (count > 1) qsort(order, (size_t)count, sizeof(order[0]), TimelineClipStartCompare);

    /* Mirror Clay's horizontal flow when tiny clips hit the readable 46px
       minimum. Transition badges use these displayed edges, not compressed
       timeline time positions, so they stay centered at low zoom. */
    float visualLeft[MAX_TIMELINE_CLIPS] = {0};
    float visualWidth[MAX_TIMELINE_CLIPS] = {0};
    float visualCursor = 0.f;
    for (int n = 0; n < count; n++) {
        int i = order[n];
        float x = gTimelinePan + gTimelineClips[i].start * pps;
        float width = fmaxf(46.f, gTimelineClips[i].duration * pps);
        if (lane.width > 0.f && x >= lane.width) break; /* sorted: every later clip is also off-screen */
        if (x + width <= 0.f) continue;
        float left = fmaxf(0.f, x);
        float placed = fmaxf(visualCursor, left);
        if (x < 0.f) width = fmaxf(1.f, width + x);
        visualLeft[i] = lane.x + placed;
        visualWidth[i] = width;
        visualCursor = placed + width;
    }

    if (track < 4) {
        for (int n = 0; n + 1 < count && gTransitionButtonCount < 64; n++) {
            int left = order[n], right = order[n + 1];
            float end = gTimelineClips[left].start + gTimelineClips[left].duration;
            if (fabsf(end - gTimelineClips[right].start) > .04f) continue;
            if (visualWidth[left] <= 0.f || visualWidth[right] <= 0.f) continue;
            float leftEdge = visualLeft[left];
            float leftVisibleEnd = leftEdge + visualWidth[left];
            float rightEdge = visualLeft[right];
            float rightVisibleEnd = rightEdge + visualWidth[right];
            float joinX = (leftVisibleEnd + rightEdge) * .5f;
            float spanSeconds = fminf(gTimelineClips[left].transitionDuration,
                                      fminf(gTimelineClips[left].duration, gTimelineClips[right].duration));
            float spanPixels = fmaxf(2.f, spanSeconds * pps);
            bool applied = gTimelineClips[left].transitionType[0] &&
                           strcmp(gTimelineClips[left].transitionType, "none") != 0;
            float spanStart = fmaxf(lane.x, fmaxf(leftEdge, joinX - spanPixels * .5f));
            float spanEnd = fminf(lane.x + lane.width, fminf(rightVisibleEnd, joinX + spanPixels * .5f));
            Rectangle durationRect = {spanStart,
                                      lane.y + 3.f,
                                      fmaxf(0.f, spanEnd - spanStart),
                                      fmaxf(0.f, lane.height - 6.f)};
            Rectangle rect = applied && durationRect.width > 2.f
                ? durationRect
                : (Rectangle){joinX - 12.f, lane.y + (lane.height - 24.f) * .5f, 24.f, 24.f};
            bool hovered = CheckCollisionPointRec(GetMousePosition(), rect);
            if (hovered) gHand = true;
            gTransitionButtons[gTransitionButtonCount++] = (TransitionButton){rect, durationRect, left, applied};
            if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                transitionPressed = true;
                gTransitionFocusClip = left;
                gRightTab = 2;
                PlutoTimelineSelect(left);
            }
        }
    }

    CLAY(laneId, {.layout = {.sizing = {GROW, FIX(height)}, .childGap = 0},
                  .clip = {.horizontal = true, .vertical = true}}) {
        int mediaTarget = TimelineTrackUnderPointerForDrag();
        if (gMediaDragIndex >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mediaTarget >= 0 &&
            mediaTarget == track && gMediaDragIndex < gImportedCount) {
            ImportedMedia *media = &gImported[gMediaDragIndex];
            bool mediaIsAudio = strcmp(media->kind, "audio") == 0;
            if (mediaIsAudio == !TimelineTrackIsVideo(track)) {
                float start = fmaxf(0.f, (GetMousePosition().x - lane.x - gTimelinePan) / pps);
                start = TimelineSnapStart(start, media->duration, !mediaIsAudio, -1, 0.f, false, NULL);
                int target = TimelineClosestFreeTrack(track, start, media->duration, -1, NULL);
                if (target < 4 && !TimelineVideoTrackIsVisible(target)) gTimelinePendingVideoTrack = target;
                if (TimelineTrackIsAudio(target) && target > gTimelinePendingAudioTrack) gTimelinePendingAudioTrack = target;
                Clay_BoundingBox targetBox = Clay_GetElementData(CLAY_IDI("TrackLane", target)).boundingBox;
                float x = targetBox.x + gTimelinePan + start * pps;
                float w = fmaxf(46.f, media->duration * pps);
                float left = fmaxf(targetBox.x, x), right = fminf(targetBox.x + targetBox.width, x + w);
                if (right > left) {
                    float h = fmaxf(1.f, targetBox.height - 8.f);
                    gMediaDropPreview = (Rectangle){left, targetBox.y + (targetBox.height - h) * .5f, right - left, h};
                    gMediaDropPreviewClip = -1;
                    gMediaDropPreviewMedia = gMediaDragIndex;
                    gMediaDropPreviewVisible = true;
                    gHand = true;
                }
            }
        }
        float cursor = 0.f;
        for (int n = 0; n < count; n++) {
            int i = order[n];
            float x = gTimelinePan + gTimelineClips[i].start * pps;
            float width = fmaxf(46.f, gTimelineClips[i].duration * pps);
            if (lane.width > 0.f && x >= lane.width) break; /* don't create Clay nodes for invisible clips */
            if (x + width <= 0.f) continue;
            float left = fmaxf(0.f, x);
            if (left > cursor) {
                CLAY_AUTO_ID({.layout = {.sizing = {CLAY_SIZING_FIXED(left - cursor), FIX(1)}}}) {}
                cursor = left;
            }
            if (x < 0.f) width = fmaxf(1.f, width + x);
            Clay_ElementId clipId = CLAY_IDI("TimelineClip", i);
            Bs b = Interact(clipId);
            Clay_BoundingBox bb = Clay_GetElementData(clipId).boundingBox;
            Vector2 pointer = GetMousePosition();
            bool keyframePressed = false;
            bool trimPressed = false;
            if (!transitionPressed && gTimelineTool == 0 && !gTimelineScrubbing &&
                !gTimelineContextConsumedClick && gKeyframeDragClip < 0 &&
                Clay_PointerOver(clipId) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int hitKeyframe = TimelineKeyframeHit(i, bb, pointer);
                if (hitKeyframe >= 0) {
                    keyframePressed = true;
                    gTransitionFocusClip = -1;
                    PlutoTimelineSelect(i);
                    gKeyframeDragClip = i;
                    gKeyframeDragIndex = hitKeyframe;
                    gKeyframeDragTime = gTimelineClips[i].keyframes[hitKeyframe].time;
                    gSelectedKeyframeClip = i;
                    gSelectedKeyframeTime = gKeyframeDragTime;
                    gPlayhead = gKeyframeDragTime;
                    gPreviewDirty = true;
                    gPreviewDebounce = 0.f;
                }
            }
            if (!keyframePressed && gClipTrimIndex < 0 && gClipDragIndex < 0 &&
                !transitionPressed && gTimelineTool == 0 && !gTimelineScrubbing &&
                !gTimelineContextConsumedClick && Clay_PointerOver(clipId) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                float clipLeft = lane.x + gTimelinePan + gTimelineClips[i].start * pps;
                float clipWidth = fmaxf(46.f, gTimelineClips[i].duration * pps);
                bool leftVisible = clipLeft >= lane.x - 0.5f;
                bool rightVisible = clipLeft + clipWidth <= lane.x + lane.width + 0.5f;
                float leftDistance = pointer.x - bb.x;
                float rightDistance = bb.x + bb.width - pointer.x;
                bool leftHit = leftVisible && leftDistance >= -1.f && leftDistance <= 9.f;
                bool rightHit = rightVisible && rightDistance >= -1.f && rightDistance <= 9.f;
                if (leftHit || rightHit) {
                    trimPressed = true;
                    gTransitionFocusClip = -1;
                    memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                    gTimelineMultiSelect[i] = true;
                    gTimelineSelectionAnchor = i;
                    PlutoTimelineSelect(i);
                    gClipTrimIndex = i;
                    gClipTrimEdge = leftHit && (!rightHit || leftDistance <= rightDistance) ? -1 : 1;
                    gClipTrimOrigStart = gTimelineClips[i].start;
                    gClipTrimOrigEnd = gTimelineClips[i].start + gTimelineClips[i].duration;
                    gClipTrimOrigOffset = gTimelineClips[i].sourceOffset;
                    gClipTrimSourceDuration = gTimelineClips[i].sourceDuration;
                    gClipTrimMouseX = pointer.x;
                    gClipTrimPixelsPerSecond = fmaxf(1.f, clipWidth / fmaxf(0.05f, gTimelineClips[i].duration));
                }
            }
            if (gKeyframeDragClip == i && gKeyframeDragIndex >= 0 &&
                (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
                float rawTime = gTimelineClips[i].start +
                    (pointer.x - bb.x) / fmaxf(1.f, bb.width) * gTimelineClips[i].duration;
                float frameRate = (float)fmaxf(1, gPreviewFps);
                float nextTime = roundf(rawTime * frameRate) / frameRate;
                nextTime = fmaxf(gTimelineClips[i].start,
                    fminf(gTimelineClips[i].start + gTimelineClips[i].duration, nextTime));
                if (fabsf(nextTime - gKeyframeDragTime) > 0.0005f) {
                    if (PlutoTimelineMoveKeyframe(i, gKeyframeDragTime, nextTime, true)) {
                        gKeyframeDragTime = nextTime;
                        gSelectedKeyframeClip = i;
                        gSelectedKeyframeTime = nextTime;
                        gPlayhead = nextTime;
                        PlutoApplyKeyframeValues();
                        gPreviewDirty = true;
                        gPreviewDebounce = 0.f;
                    }
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    gKeyframeDragClip = -1;
                    gKeyframeDragIndex = -1;
                }
            }
            if (!keyframePressed && !trimPressed && gClipTrimIndex < 0 && gKeyframeDragClip < 0 && !transitionPressed && !gTimelineScrubbing && !gTimelineContextConsumedClick &&
                Clay_PointerOver(clipId) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                gSelectedKeyframeClip = -1;
                gSelectedKeyframeTime = -1.f;
                gTransitionFocusClip = -1;
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    gTimelineMultiSelect[i] = true;
                    gTimelineSelectionAnchor = i;
                } else {
                    memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                    gTimelineMultiSelect[i] = true;
                    gTimelineSelectionAnchor = i;
                }
                PlutoTimelineSelect(i);
                if (gTimelineTool == 0 || gTimelineTool == 1) {
                    gClipDragIndex = i;
                    gClipDragTrack = track;
                    gTimelineDragTargetTrack = track;
                    gClipDragOrigStart = gTimelineClips[i].start;
                    gClipDragOffset = GetMousePosition().x - bb.x;
                    for (int j = 0; j < gTimelineClipCount; j++) {
                        gClipDragSnapshotStart[j] = gTimelineClips[j].start;
                        gClipDragSnapshotTrack[j] = gTimelineClips[j].track;
                    }
                }
            }
            if (gKeyframeDragClip < 0 && Clay_PointerOver(clipId) && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                gSelectedKeyframeClip = -1;
                gSelectedKeyframeTime = -1.f;
                if (!gTimelineMultiSelect[i]) {
                    memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                    gTimelineMultiSelect[i] = true;
                }
                PlutoTimelineSelect(i);
                gTimelineContextClip = i;
                gTimelineContextPos = GetMousePosition();
                gTimelineContextOpen = true;
            }
            if (gClipTrimIndex < 0 && gKeyframeDragClip < 0 && gClipDragIndex == i && (gTimelineTool == 0 || gTimelineTool == 1)) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && lane.width > 0.f) {
                    int hoveredTrack = TimelineTrackUnderPointerForDrag();
                    int requestedTrack = hoveredTrack >= 0 && TimelineTrackIsVideo(hoveredTrack) == TimelineTrackIsVideo(gClipDragTrack)
                        ? hoveredTrack : gClipDragTrack;
                    Clay_BoundingBox targetLane = Clay_GetElementData(CLAY_IDI("TrackLane", requestedTrack)).boundingBox;
                    float start = fmaxf(0.f, (GetMousePosition().x - targetLane.x - gTimelinePan - gClipDragOffset) / pps);
                    start = TimelineSnapStart(start, gTimelineClips[i].duration, TimelineTrackIsVideo(gClipDragTrack), i, gPlayhead, true,
                                              gTimelineClips[i].groupId[0] ? gTimelineClips[i].groupId : NULL);
                    int targetTrack = TimelineClosestFreeTrack(requestedTrack, start, gTimelineClips[i].duration, i,
                                                               gTimelineClips[i].groupId[0] ? gTimelineClips[i].groupId : NULL);
                    if (TimelineTrackIsAudio(targetTrack) && targetTrack > gTimelinePendingAudioTrack) gTimelinePendingAudioTrack = targetTrack;
                    Clay_BoundingBox targetBox = Clay_GetElementData(CLAY_IDI("TrackLane", targetTrack)).boundingBox;
                    float x = targetBox.x + gTimelinePan + start * pps;
                    float w = fmaxf(46.f, gTimelineClips[i].duration * pps);
                    float left = fmaxf(targetBox.x, x), right = fminf(targetBox.x + targetBox.width, x + w);
                    if (right > left) {
                        float h = fmaxf(1.f, targetBox.height - 8.f);
                        gMediaDropPreview = (Rectangle){left, targetBox.y + (targetBox.height - h) * .5f, right - left, h};
                        gMediaDropPreviewClip = i;
                        gMediaDropPreviewMedia = gTimelineClips[i].mediaIndex;
                        gMediaDropPreviewVisible = true;
                        gHand = true;
                    }
                } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    int hoveredTrack = TimelineTrackUnderPointerForDrag();
                    int requestedTrack = hoveredTrack >= 0 && TimelineTrackIsVideo(hoveredTrack) == TimelineTrackIsVideo(gClipDragTrack)
                        ? hoveredTrack : gClipDragTrack;
                    Clay_BoundingBox targetLane = Clay_GetElementData(CLAY_IDI("TrackLane", requestedTrack)).boundingBox;
                    float start = targetLane.width > 0.f
                        ? fmaxf(0.f, (GetMousePosition().x - targetLane.x - gTimelinePan - gClipDragOffset) / pps)
                        : gTimelineClips[i].start;
                    start = TimelineSnapStart(start, gTimelineClips[i].duration, TimelineTrackIsVideo(gClipDragTrack), i, gPlayhead, true,
                                              gTimelineClips[i].groupId[0] ? gTimelineClips[i].groupId : NULL);
                    int targetTrack = TimelineClosestFreeTrack(requestedTrack, start, gTimelineClips[i].duration, i,
                                                               gTimelineClips[i].groupId[0] ? gTimelineClips[i].groupId : NULL);
                    if (!PlutoTimelineMove(i, start, targetTrack)) {
                        gTimelineClips[i].start = gClipDragOrigStart;
                        gTimelineClips[i].track = gClipDragTrack;
                        PlutoTimelineCall("{\"command\":\"timeline_get\"}");
                    }
                    gClipDragIndex = -1;
                    gTimelinePendingVideoTrack = -1;
                    gTimelinePendingAudioTrack = -1;
                    gTimelineDragTargetTrack = -1;
                    gMediaDropPreviewVisible = false;
                    gMediaDropPreviewClip = -1;
                    gMediaDropPreviewMedia = -1;
                }
            }
            if (b.clicked && gTimelineTool == 2) {
                float cutAt = (GetMousePosition().x - lane.x - gTimelinePan) / pps;
                cutAt = fmaxf(gTimelineClips[i].start + .001f,
                              fminf(gTimelineClips[i].start + gTimelineClips[i].duration - .001f, cutAt));
                PlutoTimelineSplit(i, cutAt);
            }
            float cardWidth = fmaxf(1.f, width);
            bool clipSelected = gTimelineSelected == i || gTimelineMultiSelect[i];
            if (Clay_PointerOver(clipId) && gTimelineTool == 0 && gClipTrimIndex < 0) {
                float leftDistance = pointer.x - bb.x;
                float rightDistance = bb.x + bb.width - pointer.x;
                if ((leftDistance >= -1.f && leftDistance <= 9.f) ||
                    (rightDistance >= -1.f && rightDistance <= 9.f)) gResize = 1;
            }
            CLAY(clipId, {.layout = {.sizing = {CLAY_SIZING_FIXED(cardWidth), FIX(fmaxf(1.f, lane.height - 8.f))}, .padding = {8, 5, 5, 0}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_TOP}},
                          .clip = {.horizontal = true, .vertical = true},
                          .custom = {.customData = NC(K_CLIP, RGB(112, 52, 140), RGB(78, 36, 108), C_CARD, 0, b.hover,
                                                     (i << 1) | (clipSelected ? 1 : 0))}}) {
                if (TimelineTrackIsVideo(gTimelineClips[i].track)) {
                    Icon(I_FILM, 13, C_TEXT);
                    if (cardWidth >= 92.f) Tx(gTimelineClips[i].label, 13, C_TEXT, 1);
                }
            }
            cursor = fmaxf(cursor, left + width);
        }
        HSPACE
    }
}

static int TimelineContextActions(int actions[4]) {
    if (gTimelineContextClip < 0 || gTimelineContextClip >= gTimelineClipCount) return 0;
    TimelineClipUi *clip = &gTimelineClips[gTimelineContextClip];
    int count = 0;
    actions[count++] = clip->groupId[0] ? 1 : 0; /* 0 group, 1 ungroup */
    if (TimelineTrackIsVideo(clip->track) && clip->hasAudio) actions[count++] = 2;
    if (clip->linkedId[0]) actions[count++] = 3;
    actions[count++] = 4; /* delete */
    return count;
}

static const char *TimelineContextLabel(int action, int selectedCount) {
    switch (action) {
    case 0: return selectedCount > 1 ? TextFormat("Group %d clips", selectedCount) : "Group selected clips";
    case 1: return "Ungroup clips";
    case 2: return "Separate audio";
    case 3: return "Unlink audio";
    default: return "Delete clip";
    }
}

static Rectangle TimelineContextBounds(void) {
    int actions[4], count = TimelineContextActions(actions);
    float width = 188.f, height = count * 30.f + 8.f;
    float x = gTimelineContextPos.x, y = gTimelineContextPos.y;
    if (x + width > GetScreenWidth() - 8.f) x = GetScreenWidth() - width - 8.f;
    if (y + height > GetScreenHeight() - 8.f) y = GetScreenHeight() - height - 8.f;
    if (x < 8.f) x = 8.f;
    if (y < 8.f) y = 8.f;
    return (Rectangle){x, y, width, height};
}

static void TimelineContextLogic(void) {
    gTimelineContextConsumedClick = false;
    if (!gTimelineContextOpen) return;
    if (gTimelineContextClip < 0 || gTimelineContextClip >= gTimelineClipCount) { gTimelineContextOpen = false; return; }
    Rectangle menu = TimelineContextBounds();
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, menu)) {
            int actions[4], count = TimelineContextActions(actions);
            int row = (int)((mouse.y - menu.y - 4.f) / 30.f);
            if (row >= 0 && row < count) {
                int action = actions[row];
                if (action == 0) {
                    int selected = 0; for (int i = 0; i < gTimelineClipCount; i++) selected += gTimelineMultiSelect[i] ? 1 : 0;
                    if (selected > 1) PlutoTimelineGroupSelected();
                } else if (action == 1) PlutoTimelineUngroup(gTimelineContextClip);
                else if (action == 2) PlutoTimelineSeparateAudio(gTimelineContextClip);
                else if (action == 3) PlutoTimelineUnlink(gTimelineContextClip);
                else if (action == 4) {
                    PlutoTimelineDelete(gTimelineContextClip);
                    memset(gTimelineMultiSelect, 0, sizeof(gTimelineMultiSelect));
                    gTimelineSelected = -1;
                }
            }
            gTimelineContextOpen = false;
            gTimelineContextConsumedClick = true;
        } else {
            gTimelineContextOpen = false;
        }
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !CheckCollisionPointRec(mouse, menu)) {
        gTimelineContextOpen = false;
    }
}

static void DrawTimelineContextMenu(void) {
    if (!gTimelineContextOpen) return;
    int actions[4], count = TimelineContextActions(actions);
    Rectangle menu = TimelineContextBounds();
    Vector2 mouse = GetMousePosition();
    gTimelineContextRect = menu;
    if (CheckCollisionPointRec(mouse, menu)) gHand = true;
    FillRR(menu, 9.f, (Color){25, 26, 30, 250});
    StrokeRR(menu, 9.f, 1.f, (Color){54, 57, 65, 255});
    int selected = 0; for (int i = 0; i < gTimelineClipCount; i++) selected += gTimelineMultiSelect[i] ? 1 : 0;
    for (int row = 0; row < count; row++) {
        Rectangle item = {menu.x + 4.f, menu.y + 4.f + row * 30.f, menu.width - 8.f, 30.f};
        bool hover = CheckCollisionPointRec(mouse, item);
        if (hover) FillRR(item, 6.f, (Color){41, 43, 50, 255});
        bool disabled = actions[row] == 0 && selected < 2;
        Color text = disabled ? (Color){111, 114, 122, 255} : actions[row] == 4 ? (Color){233, 112, 118, 255} : (Color){224, 226, 231, 255};
        TxtAt(0, TimelineContextLabel(actions[row], selected), item.x + 11.f, item.y + 6.f, 13.f, text);
    }
}

static void Timeline(void) {
    gTransitionButtonCount = 0;
    TimelinePrepareSnapCache();
    TimelineContextLogic();
    TimelineUpdateTrimDrag();
    gMediaDropPreviewVisible = false;
    gMediaDropPreviewClip = -1;
    gMediaDropPreviewMedia = -1;
    TimelineRebuildVisibleVideoTracks();
    TimelineExpandVideoTracksForDrag();
    TimelineRebuildVisibleAudioTracks();
    TimelineExpandAudioTracksForDrag();
    TimelineRebuildVisibleAudioTracks();
    float pps = TimelinePps();
    Vector2 mouse = GetMousePosition();
    Clay_BoundingBox timelineBounds = Clay_GetElementData(CLAY_ID("Timeline")).boundingBox;
    Clay_BoundingBox body = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox;
    Clay_BoundingBox ruler = Clay_GetElementData(CLAY_ID("Ruler")).boundingBox;
    Clay_BoundingBox trackViewport = Clay_GetElementData(CLAY_ID("TrackViewport")).boundingBox;

    /* Dragging near the viewport edges scrolls the timeline underneath the
       pointer. This keeps long media drops and clip moves from feeling like
       they hit an invisible horizontal wall. */
    if ((gClipDragIndex >= 0 || gMediaDragIndex >= 0) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        trackViewport.width > 0.f && mouse.y >= trackViewport.y &&
        mouse.y <= trackViewport.y + trackViewport.height) {
        const float edgeZone = 38.f;
        float depth = 0.f;
        bool toEarlier = false;
        if (mouse.x < trackViewport.x + edgeZone) {
            depth = (trackViewport.x + edgeZone - mouse.x) / edgeZone;
            toEarlier = true;
        } else if (mouse.x > trackViewport.x + trackViewport.width - edgeZone) {
            depth = (mouse.x - (trackViewport.x + trackViewport.width - edgeZone)) / edgeZone;
        }
        if (depth > 0.f) {
            float pxPerSecond = 250.f + 1150.f * fminf(1.5f, depth);
            gTimelinePan += (toEarlier ? 1.f : -1.f) * pxPerSecond * gDt;
            if (toEarlier) gTimelinePan = fminf(0.f, gTimelinePan);
        }
    }

    float playheadX = ruler.x + gTimelinePan + gPlayhead * pps;
    bool grabbedPlayhead = mouse.y >= ruler.y && mouse.y <= body.y + body.height && fabsf(mouse.x - playheadX) <= 9.f;
    bool overKeyframe = TimelineKeyframeAtPointer(mouse);
    if (!overKeyframe && (Clay_PointerOver(CLAY_ID("Ruler")) || grabbedPlayhead) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gTimelineScrubbing = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gTimelineScrubbing = false;
    if (gTimelineScrubbing) {
        Clay_BoundingBox bb = Clay_GetElementData(CLAY_ID("Ruler")).boundingBox;
        if (bb.width > 0.f) {
            float seconds = fmaxf(0.f, (mouse.x - bb.x - gTimelinePan) / pps);
            { const float scrubFps = (float)fmaxf(1, gPreviewFps); gPlayhead = floorf(seconds * scrubFps + .5f) / scrubFps; }
            gPreviewDirty = true;
        }
    }

    if (gMediaDragIndex >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        int targetTrack = TimelineTrackUnderPointerForDrag();
        if (targetTrack >= 0 && gMediaDragIndex < gImportedCount) {
            ImportedMedia *media = &gImported[gMediaDragIndex];
            bool audioTrack = targetTrack >= 4;
            if (audioTrack == (strcmp(media->kind, "audio") == 0)) {
                Clay_BoundingBox lane = Clay_GetElementData(CLAY_IDI("TrackLane", targetTrack)).boundingBox;
                float start = lane.width > 0.f ? fmaxf(0.f, (mouse.x - lane.x - gTimelinePan) / pps) : 0.f;
                start = TimelineSnapStart(start, media->duration, !audioTrack, -1, 0.f, false, NULL);
                targetTrack = TimelineClosestFreeTrack(targetTrack, start, media->duration, -1, NULL);
                PlutoTimelineAddMedia(gMediaDragIndex, start, targetTrack);
            }
        }
        gMediaDragIndex = -1;
        gTimelinePendingVideoTrack = -1;
        gTimelinePendingAudioTrack = -1;
        gTimelineDragTargetTrack = -1;
    }

    /* Middle mouse is a temporary hand tool. It never changes the selected tool. */
    bool inTimelineBody = body.width > 0.f && CheckCollisionPointRec(mouse, (Rectangle){body.x, body.y, body.width, body.height});
    bool inTimelinePanel = timelineBounds.width > 0.f && CheckCollisionPointRec(mouse,
        (Rectangle){timelineBounds.x, timelineBounds.y, timelineBounds.width, timelineBounds.height});
    if (gCtrlDown && !gTextInputFocused && inTimelineBody && fabsf(gWheelDelta) > .001f)
        gTimelineLaneHeight = fmaxf(TIMELINE_LANE_MIN, fminf(TIMELINE_LANE_MAX, gTimelineLaneHeight + gWheelDelta * 3.5f));
    if (gCtrlDown && !gTextInputFocused && !gTimelinePlaying && inTimelinePanel) {
        bool zoomTrackIn = IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD);
        bool zoomTrackOut = IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT);
        if (zoomTrackIn) gTimelineLaneHeight = fminf(TIMELINE_LANE_MAX, gTimelineLaneHeight + 2.f);
        if (zoomTrackOut) gTimelineLaneHeight = fmaxf(TIMELINE_LANE_MIN, gTimelineLaneHeight - 2.f);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && inTimelinePanel) {
        gTimelineMiddlePanning = true;
        gTimelinePanMouse = mouse.x;
        gTimelinePanMouseY = mouse.y;
        gTimelinePanOrig = gTimelinePan;
        gTimelineVerticalPanOrig = gTimelineVerticalScrollT;
    }
    if (gTimelineMiddlePanning && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        gTimelinePan = fminf(0.f, gTimelinePanOrig + mouse.x - gTimelinePanMouse);
        gTimelineVerticalScrollT = gTimelineVerticalPanOrig - (mouse.y - gTimelinePanMouseY);
        gHand = true;
    }
    if (gTimelineMiddlePanning && IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) gTimelineMiddlePanning = false;

    if (gTimelineTool == 1) {
        bool overTimeline = Clay_PointerOver(CLAY_ID("Ruler")) || TimelineTrackUnderPointer() >= 0;
        bool overClip = false;
        for (int i = 0; i < gTimelineClipCount; i++) if (Clay_PointerOver(CLAY_IDI("TimelineClip", i))) { overClip = true; break; }
        if (overTimeline && !overClip && !gTimelineScrubbing && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            gTimelinePanning = true;
            gTimelinePanMouse = mouse.x;
            gTimelinePanMouseY = mouse.y;
            gTimelinePanOrig = gTimelinePan;
            gTimelineVerticalPanOrig = gTimelineVerticalScrollT;
        }
        if (gTimelinePanning && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            gTimelinePan = fminf(0.f, gTimelinePanOrig + mouse.x - gTimelinePanMouse);
            gTimelineVerticalScrollT = gTimelineVerticalPanOrig - (mouse.y - gTimelinePanMouseY);
            gHand = true;
        }
        if (gTimelinePanning && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gTimelinePanning = false;
    }

    /* Video and audio rows independently expand/collapse. A1 is not mounted
       until audio enters the timeline or is separated from a video clip. */
    TimelineRebuildVisibleVideoTracks();
    TimelineExpandVideoTracksForDrag();
    TimelineRebuildVisibleVideoTracks();
    TimelineUpdateTrackVisibility();
    TimelineRebuildVisibleAudioTracks();
    TimelineExpandAudioTracksForDrag();
    TimelineRebuildVisibleAudioTracks();
    TimelineUpdateAudioTrackVisibility();
    gTimelineLaneHeight = fmaxf(TIMELINE_LANE_MIN, fminf(TIMELINE_LANE_MAX, gTimelineLaneHeight));
    float trackViewportHeight = fmaxf(1.f, gTimeH - 90.5f);
    float laneContentHeight = 0.f;
    for (int i = 0; i < gTimelineVideoTracks; i++) {
        int track = gTimelineVideoTrackIds[i];
        float vis = gTimelineTrackVis[track];
        laneContentHeight += (gTimelineLaneHeight + 2.25f) * vis;
    }
    for (int i = 0; i < gTimelineAudioTracks; i++)
        laneContentHeight += (gTimelineLaneHeight + 2.25f) * gTimelineAudioTrackVis[i];
    gTimelineVerticalMax = fmaxf(0.f, laneContentHeight - trackViewportHeight);
    if (gShiftDown && !gCtrlDown && gWheelDelta != 0.f && inTimelineBody)
        gTimelineVerticalScrollT -= gWheelDelta * fmaxf(48.f, gTimelineLaneHeight * 1.25f);
    gTimelineVerticalScrollT = fmaxf(0.f, fminf(gTimelineVerticalMax, gTimelineVerticalScrollT));
    gTimelineVerticalScroll += (gTimelineVerticalScrollT - gTimelineVerticalScroll) * (1.f - expf(-22.f * gDt));
    if (fabsf(gTimelineVerticalScrollT - gTimelineVerticalScroll) < .25f)
        gTimelineVerticalScroll = gTimelineVerticalScrollT;
    gTimelineVerticalScroll = fmaxf(0.f, fminf(gTimelineVerticalMax, gTimelineVerticalScroll));

    CLAY(CLAY_ID("Timeline"), {.layout = {COLM, .sizing = {GROW, CLAY_SIZING_FIXED(gTimeH)}}, PANEL}) {
        CLAY(CLAY_ID("TlBar"), {.layout = {.sizing = {GROW, FIX(56)}, .padding = {14, 12, 0, 0}, .childGap = 4, .childAlignment = AL_Y}}) {
            if (IconBtn(CLAY_ID("ToolPtr"), I_POINTER, 36, 18, C_TEXT, gTimelineTool == 0, 9)) { gTimelineTool = 0; gTextInputFocused = false; }
            if (IconBtn(CLAY_ID("ToolHand"), I_HAND, 36, 18, C_DIM, gTimelineTool == 1, 9)) { gTimelineTool = 1; gTextInputFocused = false; }
            if (IconBtn(CLAY_ID("ToolCut"), I_SCISSORS, 36, 18, C_DIM, gTimelineTool == 2, 9)) { gTimelineTool = 2; gTextInputFocused = false; }
            if (IconBtn(CLAY_ID("ToolText"), I_TEXT, 36, 18, C_DIM, gTimelineTool == 3, 9)) {
                gTimelineTool = 3;
                gRightTab = 3;
                gTextInputFocused = false;
                gScroll = gScrollT = 0.f;
            }
            GAPX(8)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(1), FIX(20)}}, .backgroundColor = RGB(46, 48, 54)}) {}
            GAPX(8)
            if (IconBtn(CLAY_ID("ToolDel"), I_TRASH, 36, 18, C_DIM, false, 9)) PlutoTimelineDelete(gTimelineSelected);
            GAPX(8)
            if (IconBtn(CLAY_ID("Undo"), I_UNDO, 34, 16, RGB(143, 146, 155), false, 8)) TimelineHistoryAction(false);
            if (IconBtn(CLAY_ID("Redo"), I_REDO, 34, 16, RGB(143, 146, 155), false, 8)) TimelineHistoryAction(true);
            HSPACE
            int keyframeOnPlayhead = TimelineKeyframeAtPlayhead(gTimelineSelected);
            CLAY(CLAY_ID("KfGroup"), {.layout = {.sizing = {FIT, FIX(36)}, .padding = {3, 3, 0, 0}, .childGap = 2, .childAlignment = AL_Y},
                                      .backgroundColor = RGB(21, 22, 26), .cornerRadius = CLAY_CORNER_RADIUS(11),
                                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                if (TimelineKeyframeToolbarButton(CLAY_ID("KfPrev"), I_KF_PREV, false, false, 30) &&
                    gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
                    float prev = -1.f;
                    TimelineClipUi *clip = &gTimelineClips[gTimelineSelected];
                    for (int i = 0; i < clip->keyframeCount; i++)
                        if (clip->keyframes[i].time < gPlayhead - .001f && (prev < 0.f || clip->keyframes[i].time > prev)) prev = clip->keyframes[i].time;
                    gPlayhead = prev >= 0.f ? prev : 0.f;
                    if (prev >= 0.f) {
                        gSelectedKeyframeClip = gTimelineSelected;
                        gSelectedKeyframeTime = prev;
                    } else {
                        gSelectedKeyframeClip = -1;
                        gSelectedKeyframeTime = -1.f;
                    }
                    gPreviewDirty = true; gPreviewDebounce = 0.f;
                }
                if (TimelineKeyframeToolbarButton(CLAY_ID("KfToggle"),
                            keyframeOnPlayhead >= 0 ? I_KF_DIAMOND_MINUS : I_KF_DIAMOND,
                            keyframeOnPlayhead >= 0, true, 32) && gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
                    if (keyframeOnPlayhead >= 0) {
                        float at = gTimelineClips[gTimelineSelected].keyframes[keyframeOnPlayhead].time;
                        PlutoTimelineDeleteKeyframe(gTimelineSelected, at);
                    } else if (PlutoTimelineAddKeyframe(gPlayhead)) {
                        gSelectedKeyframeClip = gTimelineSelected;
                        gSelectedKeyframeTime = gPlayhead;
                    }
                }
                if (TimelineKeyframeToolbarButton(CLAY_ID("KfNext"), I_KF_NEXT, false, false, 30) &&
                    gTimelineSelected >= 0 && gTimelineSelected < gTimelineClipCount) {
                    float next = -1.f;
                    TimelineClipUi *clip = &gTimelineClips[gTimelineSelected];
                    for (int i = 0; i < clip->keyframeCount; i++)
                        if (clip->keyframes[i].time > gPlayhead + .001f && (next < 0.f || clip->keyframes[i].time < next)) next = clip->keyframes[i].time;
                    gPlayhead = next >= 0.f ? next : TimelineDuration();
                    if (next >= 0.f) {
                        gSelectedKeyframeClip = gTimelineSelected;
                        gSelectedKeyframeTime = next;
                    } else {
                        gSelectedKeyframeClip = -1;
                        gSelectedKeyframeTime = -1.f;
                    }
                    gPreviewDirty = true; gPreviewDebounce = 0.f;
                }
            }
            GAPX(14)
            if (IconBtn(CLAY_ID("ZoomOut"), I_ZOOM_OUT, 28, 16, C_DIM, false, 8)) gTimelineZoom = fmaxf(.35f, gTimelineZoom / 1.15f);
            Clay_ElementId zid = CLAY_ID("ZoomSlider");
            Bs zb = Interact(zid);
            if (Clay_PointerOver(zid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = zid.id;
            if (gDragId == zid.id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Clay_BoundingBox bb = Clay_GetElementData(zid).boundingBox;
                float t = bb.width > 1.f ? (mouse.x - bb.x) / bb.width : 0.f;
                t = fmaxf(0.f, fminf(1.f, t));
                gTimelineZoom = .35f + t * 3.65f;
            }
            CLAY(zid, {.layout = {.sizing = {FIX(125), FIX(20)}}, .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(150, 152, 160), C_CARD, (gTimelineZoom - .35f) / 3.65f, zb.hover * 1.5f, 0)}}) {}
            if (IconBtn(CLAY_ID("ZoomIn"), I_ZOOM_IN, 28, 16, C_DIM, false, 8)) gTimelineZoom = fminf(4.f, gTimelineZoom * 1.15f);
            GAPX(6)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(1), FIX(20)}}, .backgroundColor = RGB(45, 47, 52)}) {}
            GAPX(6)
            Clay_ElementId mid = CLAY_ID("Snap");
            Bs mb = Interact(mid);
            if (mb.clicked) PlutoTimelineSnap(!gTimelineSnap);
            CLAY(mid, {.layout = {.sizing = {FIX(36), FIX(36)}, .childAlignment = AL_C},
                       .backgroundColor = gTimelineSnap ? BtnBg(RGB(38, 40, 47), RGB(46, 48, 56), mb) : BtnBg(RGB(30, 31, 36), RGB(42, 44, 50), mb),
                       .cornerRadius = CLAY_CORNER_RADIUS(10),
                       .border = {.color = gTimelineSnap ? RGB(96, 100, 112) : RGB(48, 50, 58), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                Icon(I_MAGNET, 17, gTimelineSnap ? C_TEXT : C_ICON);
            }
        }
        CLAY(CLAY_ID("TimeBody"), {.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = {GROW, GROW}}}) {
            CLAY(CLAY_ID("TimeTrackColumn"), {.layout = {COLM, .sizing = {GROW, GROW}}}) {
                CLAY(CLAY_ID("RulerRow"), {.layout = {.sizing = {GROW, FIX(30)}}}) {
                    GAPX(60)
                    CLAY(CLAY_ID("Ruler"), {.layout = {.sizing = {GROW, GROW}},
                                             .custom = {.customData = NC(K_RULER, C_CARD, C_CARD, C_CARD, 0,
                                                                  fmaxf(0.f, gTimeH - 90.5f), 0)}}) {}
                }
                CLAY(CLAY_ID("TrackViewport"), {.layout = {COLM, .sizing = {GROW, GROW}},
                    .clip = {.horizontal = true, .vertical = true, .childOffset = {0, -gTimelineVerticalScroll}}}) {
                    for (int i = 0; i < gTimelineVideoTracks; i++) {
                        int track = gTimelineVideoTrackIds[i];
                        TimelineTrackRow(track, fmaxf(0.1f, gTimelineLaneHeight * gTimelineTrackVis[track]));
                    }
                    for (int track = 0; track < gTimelineAudioTracks; track++)
                        TimelineTrackRow(TIMELINE_MAX_VIDEO_TRACKS + track,
                            fmaxf(0.1f, gTimelineLaneHeight * gTimelineAudioTrackVis[track]));
                }
            }
            CLAY(CLAY_ID("TrackHeightRail"), {.layout = {COLM, .sizing = {FIX(24), GROW}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(1), FIX(30)}}}) {}
                Clay_ElementId heightId = CLAY_ID("TrackHeightZoom");
                Bs hb = Interact(heightId);
                if (hb.hover > .01f || gDragId == heightId.id) gResize = 2;
                if (Clay_PointerOver(heightId) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = heightId.id;
                if (gDragId == heightId.id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    Clay_BoundingBox hbb = Clay_GetElementData(heightId).boundingBox;
                    float t = hbb.height > 20.f ? 1.f - (mouse.y - hbb.y - 10.f) / (hbb.height - 20.f) : 0.f;
                    t = fmaxf(0.f, fminf(1.f, t));
                    gTimelineLaneHeight = TIMELINE_LANE_MIN + t * (TIMELINE_LANE_MAX - TIMELINE_LANE_MIN);
                }
                float laneT = (gTimelineLaneHeight - TIMELINE_LANE_MIN) / (TIMELINE_LANE_MAX - TIMELINE_LANE_MIN);
                CLAY(heightId, {.layout = {.sizing = {GROW, GROW}},
                    .custom = {.customData = NC(K_VSLIDER, RGB(38, 39, 45), RGB(154, 157, 166), C_CARD, laneT, hb.hover, 0)}}) {}
            }
        }
    }
}
