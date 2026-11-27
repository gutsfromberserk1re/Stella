use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};

const MIN_TIMELINE_SECONDS: f64 = 60.0;
const EPSILON: f64 = 1e-7;

fn default_true() -> bool { true }

#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub enum TrackId {
    #[serde(rename = "v1")]
    V1,
    #[serde(rename = "v2")]
    V2,
    #[serde(rename = "v3")]
    V3,
    #[serde(rename = "v4")]
    V4,
    #[serde(rename = "v5")]
    V5,
    #[serde(rename = "v6")]
    V6,
    #[serde(rename = "a1")]
    A1,
    #[serde(rename = "a2")]
    A2,
    #[serde(rename = "a3")]
    A3,
    #[serde(rename = "a4")]
    A4,
    #[serde(rename = "a5")]
    A5,
    #[serde(rename = "a6")]
    A6,
}

impl TrackId {
    pub fn is_video(self) -> bool {
        matches!(self, Self::V1 | Self::V2 | Self::V3 | Self::V4 | Self::V5 | Self::V6)
    }

    fn lanes(self) -> &'static [TrackId] {
        if self.is_video() {
            &[Self::V1, Self::V2, Self::V3, Self::V4, Self::V5, Self::V6]
        } else {
            &[Self::A1, Self::A2, Self::A3, Self::A4, Self::A5, Self::A6]
        }
    }

    /// Stable lane order shared by timeline serialization, preview and export.
    pub const fn sort_index(self) -> usize {
        match self {
            Self::V1 => 0, Self::V2 => 1, Self::V3 => 2, Self::V4 => 3,
            Self::V5 => 4, Self::V6 => 5,
            Self::A1 => 6, Self::A2 => 7, Self::A3 => 8, Self::A4 => 9,
            Self::A5 => 10, Self::A6 => 11,
        }
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Clip {
    pub id: String,
    pub track: TrackId,
    pub start: f64,
    #[serde(rename = "dur")]
    pub duration: f64,
    pub label: String,
    #[serde(default, rename = "linkedAudioId")]
    pub linked_clip_id: Option<String>,
    #[serde(default, flatten)]
    pub extra: HashMap<String, serde_json::Value>,
}

impl Clip {
    pub fn end(&self) -> f64 {
        self.start + self.duration
    }

    fn validate(&self) -> Result<(), TimelineError> {
        if self.id.trim().is_empty() || self.label.trim().is_empty() {
            return Err(TimelineError::InvalidClip(
                "clip id and label are required".into(),
            ));
        }
        if !self.start.is_finite() || self.start < 0.0 {
            return Err(TimelineError::InvalidClip(
                "start must be finite and non-negative".into(),
            ));
        }
        if !self.duration.is_finite() || self.duration <= 0.0 {
            return Err(TimelineError::InvalidClip(
                "duration must be finite and positive".into(),
            ));
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Timeline {
    pub clips: Vec<Clip>,
    pub duration_seconds: f64,
    pub snapping: bool,
    #[serde(default = "default_true")]
    pub magnetic: bool,
}

impl Default for Timeline {
    fn default() -> Self {
        Self {
            clips: Vec::new(),
            duration_seconds: MIN_TIMELINE_SECONDS,
            snapping: true,
            magnetic: true,
        }
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(tag = "kind", content = "message", rename_all = "camelCase")]
pub enum TimelineError {
    InvalidClip(String),
    DuplicateId(String),
    ClipNotFound(String),
    InvalidRange,
    NoFreeTrack,
}

/// Video lanes share one magnetic storyline. Different video tracks may still
/// overlap intentionally (picture-in-picture, overlays, titles); audio lanes
/// remain independent magnetic sequences.
fn same_magnetic_lane(a: TrackId, b: TrackId) -> bool {
    (a.is_video() && b.is_video()) || a == b
}

fn magnetic_lane_key(track: TrackId) -> TrackId {
    if track.is_video() { TrackId::V1 } else { track }
}

/// Keyframe `time` values are absolute timeline timestamps. Keep them attached
/// to their clip whenever a timeline operation changes that clip's start.
fn clip_is_title(clip: &Clip) -> bool {
    clip.extra.get("props")
        .and_then(|props| props.get("text"))
        .and_then(|text| text.get("isTitle"))
        .and_then(serde_json::Value::as_bool)
        .unwrap_or(false)
}

fn clip_is_subtitle(clip: &Clip) -> bool {
    clip.extra.get("props")
        .and_then(|props| props.get("text"))
        .and_then(|text| text.get("isSubtitle"))
        .and_then(serde_json::Value::as_bool)
        .unwrap_or(false)
}

fn shift_clip_keyframes(clip: &mut Clip, delta: f64) {
    if !delta.is_finite() || delta.abs() <= EPSILON {
        return;
    }
    let Some(keyframes) = clip
        .extra
        .get_mut("keyframes")
        .and_then(serde_json::Value::as_array_mut)
    else {
        return;
    };
    for keyframe in keyframes {
        let Some(time) = keyframe
            .get("time")
            .and_then(serde_json::Value::as_f64)
        else {
            continue;
        };
        if time.is_finite() {
            keyframe["time"] = serde_json::json!(time + delta);
        }
    }
}

impl Timeline {
    pub fn new(clips: Vec<Clip>) -> Result<Self, TimelineError> {
        let mut timeline = Self {
            clips,
            ..Self::default()
        };
        timeline.validate_all()?;
        timeline.refresh_duration();
        Ok(timeline)
    }

    pub fn visible_tracks(&self) -> HashSet<TrackId> {
        let mut tracks = HashSet::from([TrackId::V1]);
        tracks.extend(self.clips.iter().filter(|clip| !clip_is_subtitle(clip)).map(|clip| clip.track));
        tracks
    }

    pub fn add_clip(&mut self, mut clip: Clip) -> Result<(), TimelineError> {
        clip.validate()?;
        if self.clips.iter().any(|current| current.id == clip.id) {
            return Err(TimelineError::DuplicateId(clip.id));
        }

        let subtitle = clip_is_subtitle(&clip);
        let title = clip_is_title(&clip);
        let requested_track = clip.track;
        let same_destination_sequence = |current: &Clip| {
            !clip_is_subtitle(current)
                && (if title { current.track == requested_track && !clip_is_title(current) }
                    else { same_magnetic_lane(current.track, requested_track) && !clip_is_title(current) })
        };
        let mut lane_has_content = self.clips.iter().any(same_destination_sequence);

        /* V1-V6 share one magnetic video storyline. Audio rows remain independent.
           Compact legacy gaps before choosing the global video cut. */
        if !subtitle && self.magnetic && lane_has_content {
            self.compact_lane_kind(requested_track, &HashSet::new(), title);
            lane_has_content = self.clips.iter().any(same_destination_sequence);
        }

        let mut snapped_to_another_lane = false;
        /* Cross-row snapping is a proximity aid on every destination row. The
           threshold is strict; distant splits never become magnetic targets. */
        if !subtitle && !title && self.snapping {
            let previous_start = clip.start;
            let (snapped_start, snap_engaged) = self.snap_start(
                &clip.id, clip.start.max(0.0), clip.duration, clip.track,
                0.0, 0.25, false, None,
            );
            clip.start = snapped_start;
            snapped_to_another_lane = snap_engaged;
            let keyframe_delta = clip.start - previous_start;
            shift_clip_keyframes(&mut clip, keyframe_delta);
        }

        if !subtitle && self.magnetic && lane_has_content {
            let lane_cut = self.magnetic_start(&clip.id, clip.start, clip.duration, clip.track, None, title);
            /* A cross-row snap remains valid only when it is also a cut point
               on this row; otherwise it would leave a gap in the destination. */
            if !snapped_to_another_lane || (lane_cut - clip.start).abs() > EPSILON {
                let previous_start = clip.start;
                clip.start = lane_cut;
                let keyframe_delta = clip.start - previous_start;
                shift_clip_keyframes(&mut clip, keyframe_delta);
            }
        } else if !subtitle && self.magnetic && !snapped_to_another_lane {
            clip.start = clip.start.max(0.0);
        }

        let needs_ripple = !subtitle && self.lane_overlaps(&clip.id, clip.track, clip.start, clip.duration);
        let id = clip.id.clone();
        self.clips.push(clip);
        if needs_ripple && self.magnetic {
            self.ripple_lane_after(&id)?;
        }
        if !subtitle && self.magnetic {
            self.compact_lane_kind(requested_track, &HashSet::new(), title);
        }
        self.refresh_duration();
        Ok(())
    }

    /// Move a clip between lanes of the same media kind. Video lanes share one
    /// magnetic sequence; audio lanes remain independent. Same-row collisions
    /// ripple, while cross-row overlaps are preserved as intentional overlays.
    pub fn move_clip(
        &mut self,
        id: &str,
        desired_start: f64,
        desired_track: TrackId,
        playhead: f64,
        snap_threshold: f64,
    ) -> Result<Clip, TimelineError> {
        if !desired_start.is_finite()
            || !playhead.is_finite()
            || !snap_threshold.is_finite()
            || snap_threshold < 0.0
        {
            return Err(TimelineError::InvalidRange);
        }
        let original = self.find(id)?.clone();
        if original.track.is_video() != desired_track.is_video() {
            return Err(TimelineError::InvalidRange);
        }
        if clip_is_subtitle(&original) {
            let mut moved = original.clone();
            /* The synthetic S lane keeps exact subtitle cue timestamps and
               must not magnetize to ordinary V6 title/video boundaries. */
            moved.start = desired_start.max(0.0);
            let keyframe_delta = moved.start - original.start;
            shift_clip_keyframes(&mut moved, keyframe_delta);
            self.replace(moved);
            self.refresh_duration();
            return Ok(self.find(id)?.clone());
        }

        let group_id = original.extra.get("groupId")
            .and_then(serde_json::Value::as_str).map(str::to_owned);
        let mut members: Vec<_> = self.clips.iter().filter(|clip| {
            !clip_is_subtitle(clip) && group_id.as_deref().is_some_and(|group| {
                clip.extra.get("groupId").and_then(serde_json::Value::as_str) == Some(group)
            })
        }).cloned().collect();
        if members.is_empty() { members.push(original.clone()); }

        /* A linked audio/video partner moves as part of the same edit unit. It
           also vacates its own lane, so gap closing must account for both rows. */
        let member_ids: HashSet<String> = members.iter().map(|clip| clip.id.clone()).collect();
        let mut moving_ids = member_ids.clone();
        for member in &members {
            if let Some(linked_id) = member.linked_clip_id.as_deref() {
                moving_ids.insert(linked_id.to_owned());
            }
        }
        let vacated: Vec<Clip> = self.clips.iter()
            .filter(|clip| moving_ids.contains(&clip.id) && !clip_is_subtitle(clip))
            .cloned().collect();

        /* Vacate the shared video sequence (or each affected audio row) before
           placing the edit unit at its new cut. */
        if self.magnetic {
            self.close_lane_gaps(&vacated, &moving_ids);
            let mut affected_tracks: Vec<TrackId> = vacated.iter()
                .filter(|clip| !clip_is_subtitle(clip))
                .map(|clip| if clip_is_title(clip) { clip.track } else { magnetic_lane_key(clip.track) })
                .chain(std::iter::once(if clip_is_title(&original) { desired_track } else { magnetic_lane_key(desired_track) }))
                .collect::<HashSet<_>>()
                .into_iter()
                .collect();
            affected_tracks.sort_by_key(|track| track.sort_index());
            for track in affected_tracks {
                self.compact_lane_kind(track, &moving_ids, clip_is_title(&original));
            }
        }

        let moving_is_title = clip_is_title(&original);
        let destination_has_content = self.clips.iter().any(|clip| {
            !clip_is_subtitle(clip) && !clip_is_title(clip)
                && (if moving_is_title { clip.track == desired_track }
                    else { same_magnetic_lane(clip.track, desired_track) })
                && !moving_ids.contains(&clip.id)
        });
        let mut start = desired_start.max(0.0);
        let mut snapped_to_another_lane = false;
        if self.snapping && !moving_is_title {
            /* Cross-row snapping is only proximity-based (snap_threshold); it
               remains available even when the destination row is populated. */
            let (snapped_start, snap_engaged) = self.snap_start(
                id, start, original.duration, desired_track, playhead,
                snap_threshold, true, group_id.as_deref(),
            );
            start = snapped_start;
            snapped_to_another_lane = snap_engaged;
        }
        if self.magnetic && destination_has_content {
            let lane_cut = self.magnetic_start(id, start, original.duration, desired_track, group_id.as_deref(), moving_is_title);
            /* Cross-row snapping is kept only when it lands on a cut in this
               destination lane too. Otherwise use the lane's nearest cut. */
            if !snapped_to_another_lane || (lane_cut - start).abs() > EPSILON {
                start = lane_cut;
            }
        }

        let lanes = original.track.lanes();
        let old_rank = lanes.iter().position(|track| *track == original.track).unwrap_or(0) as i32;
        let new_rank = lanes.iter().position(|track| *track == desired_track).unwrap_or(old_rank as usize) as i32;
        let requested_track_shift = new_rank - old_rank;
        let same_kind: Vec<_> = members.iter()
            .filter(|clip| clip.track.is_video() == original.track.is_video()).collect();
        let min_rank = same_kind.iter().map(|clip| {
            clip.track.lanes().iter().position(|track| *track == clip.track).unwrap_or(0) as i32
        }).min().unwrap_or(old_rank);
        let max_rank = same_kind.iter().map(|clip| {
            clip.track.lanes().iter().position(|track| *track == clip.track).unwrap_or(0) as i32
        }).max().unwrap_or(old_rank);
        let max_lane = lanes.len().saturating_sub(1) as i32;
        let track_shift = requested_track_shift.clamp(-min_rank, max_lane - max_rank);
        let lane_members: HashSet<_> = same_kind.iter().map(|clip| clip.id.as_str()).collect();
        let delta = start - original.start;

        for mut member in members.iter().cloned() {
            let old_member_start = member.start;
            member.start = (member.start + delta).max(0.0);
            let keyframe_delta = member.start - old_member_start;
            shift_clip_keyframes(&mut member, keyframe_delta);
            if lane_members.contains(member.id.as_str()) {
                let member_rank = member.track.lanes().iter()
                    .position(|track| *track == member.track).unwrap_or(0) as i32;
                if let Some(track) = member.track.lanes().get((member_rank + track_shift) as usize) {
                    member.track = *track;
                }
            }
            self.replace(member);
        }

        /* Move linked partners by the same timeline delta, without applying a
           second lane shift to a partner that is already an explicit group member. */
        for member in &members {
            if let Some(linked_id) = member.linked_clip_id.as_deref() {
                if !member_ids.contains(linked_id) {
                    if let Some(mut linked) = self.clips.iter().find(|clip| clip.id == linked_id).cloned() {
                        let old_linked_start = linked.start;
                        linked.start = (linked.start + delta).max(0.0);
                        let keyframe_delta = linked.start - old_linked_start;
                        shift_clip_keyframes(&mut linked, keyframe_delta);
                        self.replace(linked);
                    }
                }
            }
        }

        if self.magnetic {
            let mut affected_tracks: Vec<TrackId> = moving_ids.iter()
                .filter_map(|moving_id| self.clips.iter().find(|clip| clip.id == *moving_id))
                .filter(|clip| !clip_is_subtitle(clip))
                .map(|clip| clip.track)
                .collect::<HashSet<_>>()
                .into_iter()
                .collect();
            affected_tracks.sort_by_key(|track| track.sort_index());
            for track in affected_tracks {
                self.ripple_lane_after_group(track, &moving_ids)?;
            }
        }
        self.refresh_duration();
        self.find(id).cloned()
    }

    pub fn trim_clip(&mut self, id: &str, start: f64, end: f64, source_duration: Option<f64>) -> Result<Clip, TimelineError> {
        if !start.is_finite() || !end.is_finite() || start < 0.0 || end - start <= EPSILON {
            return Err(TimelineError::InvalidRange);
        }
        let mut clip = self.find(id)?.clone();
        if let Some(duration) = source_duration.filter(|duration| duration.is_finite() && *duration > 0.0) {
            clip.extra.insert("sourceDuration".into(), serde_json::json!(duration));
        }
        let source_offset = clip.extra.get("sourceOffset")
            .and_then(serde_json::Value::as_f64).unwrap_or(0.0).max(0.0);
        let source_duration = clip.extra.get("sourceDuration")
            .and_then(serde_json::Value::as_f64).filter(|duration| duration.is_finite() && *duration > 0.0);
        let linked_id = clip.linked_clip_id.clone();
        let linked_original = linked_id.as_deref()
            .map(|linked| self.find(linked).cloned())
            .transpose()?;
        let linked_track = linked_original.as_ref().map(|linked| linked.track);
        let linked_source_offset = linked_original.as_ref().map(|linked| {
            linked.extra.get("sourceOffset").and_then(serde_json::Value::as_f64).unwrap_or(0.0).max(0.0)
        });
        let linked_source_duration = linked_original.as_ref().and_then(|linked| {
            linked.extra.get("sourceDuration").and_then(serde_json::Value::as_f64)
                .filter(|duration| duration.is_finite() && *duration > 0.0)
        });

        /* Trim the pair on one shared timeline range, but keep each source's
           independent in-point. An audio file linked to video can start at a
           different source timestamp; changing one clip must not overwrite the
           other's sourceOffset or sourceDuration. */
        let mut minimum_start = (clip.start - source_offset).max(0.0);
        if let (Some(linked), Some(linked_offset)) = (linked_original.as_ref(), linked_source_offset) {
            minimum_start = minimum_start.max((linked.start - linked_offset).max(0.0));
        }
        let next_start = start.max(minimum_start);
        let next_offset = (source_offset + next_start - clip.start).max(0.0);
        let mut next_end = source_duration
            .map(|duration| end.min(next_start + (duration - next_offset).max(0.0)))
            .unwrap_or_else(|| end.min(clip.end()));
        let linked_next_offset = linked_original.as_ref().zip(linked_source_offset).map(|(linked, offset)| {
            (offset + next_start - linked.start).max(0.0)
        });
        if let (Some(linked), Some(linked_offset)) = (linked_original.as_ref(), linked_next_offset) {
            if let Some(duration) = linked_source_duration {
                next_end = next_end.min(next_start + (duration - linked_offset).max(0.0));
            } else if linked_source_duration.is_none() && source_duration.is_none() {
                next_end = next_end.min(linked.end() + (next_start - linked.start));
            }
        }
        if next_end - next_start <= EPSILON { return Err(TimelineError::InvalidRange); }

        let mut changing_ids = vec![id];
        if let Some(linked) = linked_id.as_deref() { changing_ids.push(linked); }
        if !clip_is_subtitle(&clip) && self.clips.iter().any(|other| {
            !changing_ids.contains(&other.id.as_str())
                && !clip_is_subtitle(other)
                && (other.track == clip.track || Some(other.track) == linked_track)
                && next_start < other.end() - EPSILON
                && next_end > other.start + EPSILON
        }) {
            return Err(TimelineError::InvalidRange);
        }
        let previous_start = clip.start;
        clip.start = next_start;
        let start_delta = clip.start - previous_start;
        shift_clip_keyframes(&mut clip, start_delta);
        clip.duration = next_end - next_start;
        clip.extra.insert("sourceOffset".into(), serde_json::json!(next_offset));
        self.replace(clip.clone());
        if let (Some(mut linked), Some(linked_offset)) = (linked_original, linked_next_offset) {
            let previous_linked_start = linked.start;
            linked.start = next_start;
            let linked_delta = linked.start - previous_linked_start;
            shift_clip_keyframes(&mut linked, linked_delta);
            linked.duration = next_end - next_start;
            linked.extra.insert("sourceOffset".into(), serde_json::json!(linked_offset));
            /* sourceDuration belongs to each source independently and is left
               untouched on the linked clip. */
            self.replace(linked);
        }
        if self.magnetic && !clip_is_subtitle(&clip) {
            /* Keep linked A/V clips aligned while closing gaps on the edited
               row. compact_lane shifts the linked partner by the same delta;
               independently packing its row could break the link. */
            self.compact_lane_kind(clip.track, &HashSet::new(), clip_is_title(&clip));
        }
        self.refresh_duration();
        self.find(id).cloned()
    }

    pub fn split_clip(
        &mut self,
        id: &str,
        at: f64,
        right_id: String,
    ) -> Result<(Clip, Clip), TimelineError> {
        if !at.is_finite()
            || right_id.trim().is_empty()
            || self.clips.iter().any(|c| c.id == right_id)
        {
            return Err(TimelineError::InvalidRange);
        }
        let mut left = self.find(id)?.clone();
        if at <= left.start + EPSILON || at >= left.end() - EPSILON {
            return Err(TimelineError::InvalidRange);
        }

        /* A linked audio/video pair is one edit unit. Split both source clips at
           exactly the same timeline time and preserve reciprocal links on both
           halves, so subsequent trims, moves and deletes remain synchronized. */
        let linked_original = if let Some(linked_id) = left.linked_clip_id.as_deref() {
            let linked = self.find(linked_id)?.clone();
            if (linked.start - left.start).abs() > 0.001
                || (linked.duration - left.duration).abs() > 0.001
                || linked.track.is_video() == left.track.is_video()
            {
                return Err(TimelineError::InvalidRange);
            }
            Some(linked)
        } else { None };
        let linked_right_id = linked_original.as_ref().map(|_| format!("{}-linked", right_id));
        if linked_right_id.as_ref().map_or(false, |new_id| self.clips.iter().any(|clip| clip.id == *new_id)) {
            return Err(TimelineError::InvalidRange);
        }

        let split_offset = at - left.start;
        let original_source_offset = left.extra.get("sourceOffset")
            .and_then(serde_json::Value::as_f64).unwrap_or(0.0);
        let mut right_extra = left.extra.clone();
        right_extra.insert("sourceOffset".into(), serde_json::json!(original_source_offset + split_offset));
        if !left.track.is_video() {
            if let Some(linked_right_id) = linked_right_id.as_ref() {
                right_extra.insert("detachedFromVideoId".into(), serde_json::json!(linked_right_id));
            }
        }
        let right = Clip {
            id: right_id,
            track: left.track,
            start: at,
            duration: left.end() - at,
            label: left.label.clone(),
            linked_clip_id: linked_right_id.clone(),
            extra: right_extra,
        };
        left.duration = at - left.start;
        if let Some(transition) = left.extra.get_mut("props").and_then(serde_json::Value::as_object_mut)
            .and_then(|props| props.get_mut("transitionOut")).and_then(serde_json::Value::as_object_mut) {
            transition.insert("type".into(), serde_json::json!("none"));
        }
        self.replace(left.clone());
        self.clips.push(right.clone());

        if let (Some(mut linked_left), Some(linked_right_id)) = (linked_original, linked_right_id) {
            let linked_original_end = linked_left.end();
            let linked_offset = at - linked_left.start;
            let linked_source_offset = linked_left.extra.get("sourceOffset")
                .and_then(serde_json::Value::as_f64).unwrap_or(0.0);
            linked_left.duration = at - linked_left.start;
            if let Some(transition) = linked_left.extra.get_mut("props").and_then(serde_json::Value::as_object_mut)
                .and_then(|props| props.get_mut("transitionOut")).and_then(serde_json::Value::as_object_mut) {
                transition.insert("type".into(), serde_json::json!("none"));
            }
            let mut linked_right = linked_left.clone();
            linked_right.id = linked_right_id;
            linked_right.start = at;
            linked_right.duration = linked_original_end - at;
            linked_right.linked_clip_id = Some(right.id.clone());
            linked_right.extra.insert("sourceOffset".into(), serde_json::json!(linked_source_offset + linked_offset));
            if linked_right.track.is_video() == false {
                linked_right.extra.insert("detachedFromVideoId".into(), serde_json::json!(right.id.clone()));
            }
            linked_left.linked_clip_id = Some(left.id.clone());
            self.replace(linked_left);
            self.clips.push(linked_right);
        }

        let left_id = left.id.clone();
        let right_id = right.id.clone();
        if self.magnetic && !clip_is_subtitle(&left) {
            self.compact_lane(left.track, &HashSet::new());
        }
        self.refresh_duration();
        Ok((self.find(&left_id)?.clone(), self.find(&right_id)?.clone()))
    }

    pub fn delete_clip(&mut self, id: &str) -> Result<Vec<Clip>, TimelineError> {
        let clip = self.find(id)?.clone();
        let mut deleted = vec![clip.clone()];
        if let Some(linked) = clip.linked_clip_id.as_deref() {
            if let Some(other) = self.clips.iter().find(|c| c.id == linked) {
                deleted.push(other.clone());
            }
        }
        let deleted_ids: HashSet<String> = deleted.iter().map(|c| c.id.clone()).collect();
        if self.magnetic && deleted.iter().any(|item| !clip_is_subtitle(item)) {
            /* Ripple-delete exactly the removed time on each affected row.
               Do not compact the whole row again here: that would also erase
               pre-existing gaps unrelated to this deletion, pulling followers
               too far left (and making A/V rows appear to jump unpredictably). */
            self.close_lane_gaps(&deleted, &deleted_ids);
        }
        self.clips.retain(|c| !deleted_ids.contains(&c.id));
        self.refresh_duration();
        Ok(deleted)
    }

    pub fn group_clips(&mut self, ids: &[String], group_id: &str) -> Result<(), TimelineError> {
        let unique: HashSet<_> = ids.iter().map(String::as_str).collect();
        if unique.len() < 2 || group_id.trim().is_empty() { return Err(TimelineError::InvalidRange); }
        for id in &unique { self.find(id)?; }
        for id in unique {
            let clip = self.clips.iter_mut().find(|clip| clip.id == id).ok_or_else(|| TimelineError::ClipNotFound(id.into()))?;
            clip.extra.insert("groupId".into(), serde_json::json!(group_id));
        }
        Ok(())
    }

    pub fn ungroup_clip(&mut self, id: &str) -> Result<(), TimelineError> {
        let group_id = self.find(id)?.extra.get("groupId").and_then(serde_json::Value::as_str)
            .ok_or(TimelineError::InvalidRange)?.to_owned();
        for clip in &mut self.clips {
            if clip.extra.get("groupId").and_then(serde_json::Value::as_str) == Some(group_id.as_str()) {
                clip.extra.remove("groupId");
            }
        }
        Ok(())
    }

    pub fn separate_audio(&mut self, id: &str, audio_id: String) -> Result<Clip, TimelineError> {
        if audio_id.trim().is_empty() || self.clips.iter().any(|clip| clip.id == audio_id) {
            return Err(TimelineError::InvalidRange);
        }
        let mut video = self.find(id)?.clone();
        if !video.track.is_video()
            || video.extra.get("hasAudio").and_then(serde_json::Value::as_bool) != Some(true)
            || video.extra.get("kind").and_then(serde_json::Value::as_str) == Some("audio")
        { return Err(TimelineError::InvalidRange); }
        let mut audio = video.clone();
        audio.id = audio_id.clone();
        audio.track = self.nearest_available_track_for_detached_audio(&audio_id, audio.start, audio.duration)
            .ok_or(TimelineError::NoFreeTrack)?;
        audio.label = format!("{} Audio", video.label);
        audio.linked_clip_id = Some(video.id.clone());
        audio.extra.insert("kind".into(), serde_json::json!("audio"));
        audio.extra.insert("hasAudio".into(), serde_json::json!(true));
        audio.extra.insert("detachedFromVideoId".into(), serde_json::json!(video.id.clone()));
        video.linked_clip_id = Some(audio_id.clone());
        video.extra.insert("hasAudio".into(), serde_json::json!(false));
        self.replace(video);
        /* The extracted stream is linked and must retain the video's exact
           timeline range; add_clip's ordinary magnetic placement is bypassed. */
        audio.validate()?;
        self.clips.push(audio);
        self.refresh_duration();
        Ok(self.find(&audio_id)?.clone())
    }

    pub fn link_clips(&mut self, video_id: &str, audio_id: &str) -> Result<(), TimelineError> {
        let mut video = self.find(video_id)?.clone();
        let mut audio = self.find(audio_id)?.clone();
        if !video.track.is_video() || audio.track.is_video()
            || video.linked_clip_id.is_some() || audio.linked_clip_id.is_some()
            || audio.extra.get("kind").and_then(serde_json::Value::as_str) != Some("audio")
            || (video.start - audio.start).abs() > 0.001
            || (video.duration - audio.duration).abs() > 0.001
        { return Err(TimelineError::InvalidRange); }
        video.linked_clip_id = Some(audio.id.clone());
        audio.linked_clip_id = Some(video.id.clone());
        video.extra.remove("unlinkedFromClipId");
        audio.extra.remove("unlinkedFromClipId");
        self.replace(video);
        self.replace(audio);
        Ok(())
    }

    pub fn relink_clip(&mut self, id: &str, path: &str, label: &str, source_duration: f64,
                       kind: &str, has_audio: bool) -> Result<Clip, TimelineError> {
        if path.trim().is_empty() || !source_duration.is_finite() || source_duration <= 0.0 {
            return Err(TimelineError::InvalidRange);
        }
        let mut clip = self.find(id)?.clone();
        let existing_kind = clip.extra.get("kind").and_then(serde_json::Value::as_str).unwrap_or("");
        let next_kind = if kind.trim().is_empty() { existing_kind } else { kind };
        if (clip.track.is_video() && next_kind == "audio")
            || (!clip.track.is_video() && next_kind != "audio")
        {
            return Err(TimelineError::InvalidRange);
        }
        if let Some(linked_id) = clip.linked_clip_id.take() {
            clip.extra.insert("unlinkedFromClipId".into(), serde_json::json!(linked_id.clone()));
            self.replace(clip.clone());
            if let Some(mut linked) = self.clips.iter().find(|candidate| candidate.id == linked_id).cloned() {
                linked.linked_clip_id = None;
                linked.extra.insert("unlinkedFromClipId".into(), serde_json::json!(clip.id.clone()));
                self.replace(linked);
            }
        }
        let offset = clip.extra.get("sourceOffset").and_then(serde_json::Value::as_f64).unwrap_or(0.0)
            .clamp(0.0, (source_duration - 0.05).max(0.0));
        clip.extra.insert("path".into(), serde_json::json!(path));
        clip.extra.insert("sourceDuration".into(), serde_json::json!(source_duration));
        clip.extra.insert("sourceOffset".into(), serde_json::json!(offset));
        if !kind.trim().is_empty() { clip.extra.insert("kind".into(), serde_json::json!(kind)); }
        clip.extra.insert("hasAudio".into(), serde_json::json!(has_audio));
        if !label.trim().is_empty() { clip.label = label.to_owned(); }
        clip.duration = clip.duration.min((source_duration - offset).max(0.05));
        self.replace(clip.clone());
        if self.magnetic && !clip_is_subtitle(&clip) {
            self.compact_lane_kind(clip.track, &HashSet::new(), clip_is_title(&clip));
            clip = self.find(id)?.clone();
        }
        self.refresh_duration();
        Ok(clip)
    }

    /// Restores the embedded audio stream on its original video clip and
    /// removes the detached timeline audio. This is intentionally limited to
    /// the exact untouched source/range created by separate_audio; an edited
    /// or replaced audio clip must not be silently discarded.
    pub fn reattach_audio(&mut self, video_id: &str, audio_id: &str) -> Result<(), TimelineError> {
        let mut video = self.find(video_id)?.clone();
        let audio = self.find(audio_id)?.clone();
        let detached_from = audio.extra.get("detachedFromVideoId").and_then(serde_json::Value::as_str);
        let expected_label = format!("{} Audio", video.label);
        let video_path = video.extra.get("path").and_then(serde_json::Value::as_str).unwrap_or("");
        let audio_path = audio.extra.get("path").and_then(serde_json::Value::as_str).unwrap_or("");
        let video_offset = video.extra.get("sourceOffset").and_then(serde_json::Value::as_f64).unwrap_or(0.0);
        let audio_offset = audio.extra.get("sourceOffset").and_then(serde_json::Value::as_f64).unwrap_or(0.0);
        let identity_matches = detached_from.map_or(audio.label == expected_label, |source_id| source_id == video_id);
        if !video.track.is_video()
            || audio.track.is_video()
            || audio.extra.get("kind").and_then(serde_json::Value::as_str) != Some("audio")
            || video.extra.get("hasAudio").and_then(serde_json::Value::as_bool) != Some(false)
            || !identity_matches
            || video_path.is_empty()
            || video_path != audio_path
            || (video.start - audio.start).abs() > 0.001
            || (video.duration - audio.duration).abs() > 0.001
            || (video_offset - audio_offset).abs() > 0.001
            || video.linked_clip_id.as_deref().map_or(false, |linked| linked != audio_id)
            || audio.linked_clip_id.as_deref().map_or(false, |linked| linked != video_id)
        {
            return Err(TimelineError::InvalidRange);
        }
        video.linked_clip_id = None;
        video.extra.remove("unlinkedFromClipId");
        video.extra.insert("hasAudio".into(), serde_json::json!(true));
        self.replace(video);
        self.clips.retain(|clip| clip.id != audio_id);
        if self.magnetic {
            self.compact_lane(audio.track, &HashSet::from([audio.id.clone()]));
        }
        self.refresh_duration();
        Ok(())
    }

    pub fn unlink_clip(&mut self, id: &str) -> Result<(), TimelineError> {
        let mut clip = self.find(id)?.clone();
        let linked_id = clip.linked_clip_id.take().ok_or(TimelineError::InvalidRange)?;
        clip.extra.insert("unlinkedFromClipId".into(), serde_json::json!(linked_id.clone()));
        self.replace(clip.clone());
        if let Some(mut linked) = self.clips.iter().find(|current| current.id == linked_id).cloned() {
            linked.linked_clip_id = None;
            linked.extra.insert("unlinkedFromClipId".into(), serde_json::json!(clip.id.clone()));
            self.replace(linked);
        }
        Ok(())
    }

    fn snap_start(
        &self,
        id: &str,
        start: f64,
        duration: f64,
        track: TrackId,
        playhead: f64,
        threshold: f64,
        snap_playhead: bool,
        exclude_group: Option<&str>,
    ) -> (f64, bool) {
        let mut best: Option<(f64, f64)> = None;
        let linked_partner_id = self.clips.iter().find(|clip| clip.id == id)
            .and_then(|clip| clip.linked_clip_id.as_deref());
        if snap_playhead {
            for offset in [0.0, duration] {
                let distance = (playhead - (start + offset)).abs();
                if distance <= threshold && best.map_or(true, |(best_distance, _)| distance < best_distance) {
                    best = Some((distance, playhead - offset));
                }
            }
        }
        for clip in self.clips.iter().filter(|clip| {
            clip.id != id
                && clip.track != track
                && linked_partner_id != Some(clip.id.as_str())
                && clip.linked_clip_id.as_deref() != Some(id)
                && !clip_is_subtitle(clip)
                && exclude_group.map_or(true, |group| {
                    clip.extra.get("groupId").and_then(serde_json::Value::as_str) != Some(group)
                })
        }) {
            for (edge, offset) in [(clip.start, 0.0), (clip.end(), 0.0),
                                   (clip.start, duration), (clip.end(), duration)] {
                let distance = (edge - (start + offset)).abs();
                if distance <= threshold && best.map_or(true, |(best_distance, _)| distance < best_distance) {
                    best = Some((distance, edge - offset));
                }
            }
        }
        best.map_or((start, false), |(_, value)| (value.max(0.0), true))
    }

    /// Magnetic insertion has no distance threshold. V1-V6 share one ordered
    /// video sequence; audio lanes are independent. Existing overlaps are valid
    /// overlay intervals, not gaps to be compacted.
    fn magnetic_start(
        &self,
        id: &str,
        start: f64,
        _duration: f64,
        track: TrackId,
        exclude_group: Option<&str>,
        title_only: bool,
    ) -> f64 {
        let mut best_start = start.max(0.0);
        let in_sequence = |clip: &&Clip| {
            !clip_is_subtitle(clip) && !clip_is_title(clip)
                && (if title_only { clip.track == track }
                    else { same_magnetic_lane(clip.track, track) })
        };
        // Dropping onto the body of a different video row is an explicit
        // overlay gesture, not a request to join the sequence at a cut.
        if track.is_video() && self.clips.iter().any(|clip| {
            !title_only && clip.track.is_video() && clip.track != track && !clip_is_subtitle(clip) && !clip_is_title(clip)
                && clip.id != id
                && exclude_group.map_or(true, |group| {
                    clip.extra.get("groupId").and_then(serde_json::Value::as_str) != Some(group)
                })
                && start > clip.start + EPSILON && start < clip.end() - EPSILON
        }) {
            return best_start;
        }
        let lane_first_start = self.clips.iter()
            .filter(|clip| in_sequence(clip)
                && clip.id != id
                && exclude_group.map_or(true, |group| {
                    clip.extra.get("groupId").and_then(serde_json::Value::as_str) != Some(group)
                }))
            .map(|clip| clip.start)
            .min_by(|a, b| a.total_cmp(b));
        /* Inserting before the first clip is a valid new head for this row;
           it must not be pulled forward to that clip's start. */
        if lane_first_start.map_or(true, |first| best_start <= first + EPSILON) {
            return best_start;
        }
        let mut best_distance = f64::INFINITY;
        for clip in self.clips.iter().filter(|clip| {
            in_sequence(&clip)
                && clip.id != id
                && exclude_group.map_or(true, |group| {
                    clip.extra.get("groupId").and_then(serde_json::Value::as_str) != Some(group)
                })
        }) {
            for candidate in [clip.start, clip.end()] {
                if !candidate.is_finite() || candidate < 0.0 { continue; }
                let starts_inside_existing = self.clips.iter().any(|other| {
                    in_sequence(&other)
                        && other.id != id
                        && exclude_group.map_or(true, |group| {
                            other.extra.get("groupId").and_then(serde_json::Value::as_str) != Some(group)
                        })
                        && candidate > other.start + EPSILON
                        && candidate < other.end() - EPSILON
                });
                if starts_inside_existing { continue; }
                let distance = (candidate - start).abs();
                if distance < best_distance - EPSILON
                    || ((distance - best_distance).abs() <= EPSILON && candidate < best_start)
                {
                    best_distance = distance;
                    best_start = candidate;
                }
            }
        }
        best_start
    }

    /// Find an audio lane that has room for an extracted linked-audio partner.
    /// The partner must keep the exact video timeline start and must not be
    /// magnetically moved to a different audio edit point after separation.
    fn nearest_available_track_for_detached_audio(
        &self,
        id: &str,
        start: f64,
        duration: f64,
    ) -> Option<TrackId> {
        [TrackId::A1, TrackId::A2, TrackId::A3, TrackId::A4, TrackId::A5, TrackId::A6]
            .into_iter()
            .find(|track| !self.clips.iter().any(|clip| {
                clip.id != id && !clip_is_subtitle(clip) && clip.track == *track
                    && start < clip.end() - EPSILON
                    && start + duration > clip.start + EPSILON
            }))
    }

    /// Close removed time in the shared video sequence, or on each affected
    /// audio row. Video overlaps on other rows keep the covered interval alive.
    fn close_lane_gaps(&mut self, removed: &[Clip], excluded_ids: &HashSet<String>) {
        let mut intervals_by_lane: HashMap<(TrackId, bool), Vec<(f64, f64)>> = HashMap::new();
        for clip in removed.iter().filter(|clip| !clip_is_subtitle(clip) && clip.duration > EPSILON) {
            let title_only = clip_is_title(clip);
            let lane = if title_only { clip.track } else { magnetic_lane_key(clip.track) };
            intervals_by_lane.entry((lane, title_only)).or_default().push((clip.start, clip.end()));
        }

        let mut shifted_ids: HashSet<String> = HashSet::new();
        let mut linked_followers_to_ripple: HashSet<String> = HashSet::new();
        let mut ordered_lanes: Vec<(TrackId, bool)> = intervals_by_lane.keys().copied().collect();
        ordered_lanes.sort_by_key(|(track, title)| (track.sort_index(), *title));
        for (lane, title_only) in ordered_lanes {
            let mut intervals = intervals_by_lane.remove(&(lane, title_only)).unwrap_or_default();
            intervals.sort_by(|a, b| a.0.total_cmp(&b.0));
            let mut removed_union: Vec<(f64, f64)> = Vec::with_capacity(intervals.len());
            for (start, end) in intervals {
                if let Some(last) = removed_union.last_mut() {
                    if start <= last.1 + EPSILON {
                        last.1 = last.1.max(end);
                        continue;
                    }
                }
                removed_union.push((start, end));
            }

            // Only remove time that is no longer covered by any surviving clip
            // in the shared magnetic lane. An overlay covering the deleted
            // interval keeps that part of the composition alive.
            let mut coverage: Vec<(f64, f64)> = self.clips.iter()
                .filter(|clip| !clip_is_subtitle(clip) && clip_is_title(clip) == title_only
                    && (if title_only { clip.track == lane } else { same_magnetic_lane(clip.track, lane) })
                    && !excluded_ids.contains(&clip.id))
                .map(|clip| (clip.start, clip.end())).collect();
            coverage.sort_by(|a, b| a.0.total_cmp(&b.0));
            let mut coverage_union: Vec<(f64, f64)> = Vec::with_capacity(coverage.len());
            for (start, end) in coverage {
                if let Some(last) = coverage_union.last_mut() {
                    if start <= last.1 + EPSILON {
                        last.1 = last.1.max(end);
                        continue;
                    }
                }
                coverage_union.push((start, end));
            }
            let mut merged: Vec<(f64, f64)> = Vec::new();
            for (removed_start, removed_end) in removed_union {
                let mut cursor = removed_start;
                for (cover_start, cover_end) in coverage_union.iter().copied()
                    .filter(|(start, end)| *end > removed_start + EPSILON && *start < removed_end - EPSILON)
                {
                    if cover_start > cursor + EPSILON {
                        merged.push((cursor, cover_start.min(removed_end)));
                    }
                    cursor = cursor.max(cover_end);
                    if cursor >= removed_end - EPSILON { break; }
                }
                if cursor < removed_end - EPSILON {
                    merged.push((cursor, removed_end));
                }
            }

            let mut followers: Vec<Clip> = self.clips.iter()
                .filter(|clip| !clip_is_subtitle(clip) && clip_is_title(clip) == title_only
                    && (if title_only { clip.track == lane } else { same_magnetic_lane(clip.track, lane) })
                    && !excluded_ids.contains(&clip.id))
                .cloned().collect();
            followers.sort_by(|a, b| a.start.total_cmp(&b.start).then_with(|| a.id.cmp(&b.id)));

            for mut clip in followers {
                if shifted_ids.contains(&clip.id) { continue; }
                let original_start = clip.start;
                let shift: f64 = merged.iter()
                    .filter(|(_, end)| *end <= original_start + EPSILON)
                    .map(|(start, end)| end - start)
                    .sum();
                if shift <= EPSILON { continue; }

                let next_start = (original_start - shift).max(0.0);
                let delta = next_start - original_start;
                if delta.abs() <= EPSILON { continue; }

                clip.start = next_start;
                shift_clip_keyframes(&mut clip, delta);
                let linked_id = clip.linked_clip_id.clone();
                shifted_ids.insert(clip.id.clone());
                self.replace(clip);
                if let Some(linked_id) = linked_id {
                    if !excluded_ids.contains(&linked_id) && shifted_ids.insert(linked_id.clone()) {
                        if let Some(mut linked) = self.clips.iter().find(|item| item.id == linked_id).cloned() {
                            linked.start = (linked.start + delta).max(0.0);
                            shift_clip_keyframes(&mut linked, delta);
                            self.replace(linked);
                            linked_followers_to_ripple.insert(linked_id);
                        }
                    }
                }
            }
        }
        let mut linked_followers: Vec<Clip> = linked_followers_to_ripple.iter()
            .filter_map(|linked_id| self.clips.iter().find(|clip| clip.id == *linked_id).cloned())
            .collect();
        linked_followers.sort_by(|a, b| a.track.sort_index().cmp(&b.track.sort_index())
            .then_with(|| a.start.total_cmp(&b.start))
            .then_with(|| a.id.cmp(&b.id)));
        for linked in linked_followers {
            let _ = self.ripple_lane_after(&linked.id);
        }

        // Do not compact here: ripple-delete removes only the deleted time.
        // Packing the row afterward would also erase unrelated pre-existing gaps.
    }

    /// Close uncovered gaps in the selected magnetic sequence. For video this
    /// spans V1-V6; overlapping clips retain their starts as intentional layers.
    /// Audio remains lane-local. The first clip keeps its absolute start.
    fn compact_lane(&mut self, track: TrackId, excluded_ids: &HashSet<String>) {
        self.compact_lane_kind(track, excluded_ids, false);
    }

    fn compact_lane_kind(&mut self, track: TrackId, excluded_ids: &HashSet<String>, title_only: bool) {
        let ordered_lane = magnetic_lane_key(track);
        let mut ordered: Vec<Clip> = self.clips.iter()
            .filter(|clip| !clip_is_subtitle(clip)
                && (if title_only { same_magnetic_lane(clip.track, ordered_lane) } else { same_magnetic_lane(clip.track, ordered_lane) && !clip_is_title(clip) })
                && !excluded_ids.contains(&clip.id))
            .cloned().collect();
        ordered.sort_by(|a, b| a.start.total_cmp(&b.start).then_with(|| a.id.cmp(&b.id)));
        let Some(first) = ordered.first() else { return; };
        let mut cursor = first.end();

        for mut clip in ordered.into_iter().skip(1) {
            // If this clip already overlaps the current video composition, keep
            // its exact start as an intentional cross-track overlay. Otherwise
            // close the gap by moving it to the end of the preceding composition.
            if clip.start > cursor + EPSILON {
                let delta = cursor - clip.start;
                clip.start = cursor;
                shift_clip_keyframes(&mut clip, delta);
                let linked_id = clip.linked_clip_id.clone();
                self.replace(clip.clone());
                if let Some(linked_id) = linked_id {
                    if !excluded_ids.contains(&linked_id) {
                        if let Some(mut linked) = self.clips.iter()
                            .find(|item| item.id == linked_id && !item.track.is_video()).cloned()
                        {
                            linked.start = (linked.start + delta).max(0.0);
                            shift_clip_keyframes(&mut linked, delta);
                            self.replace(linked);
                        }
                    }
                }
            }
            cursor = cursor.max(clip.end());
        }
    }

    /// Ripple a moved group once per destination row. Repeatedly calling the
    /// single-anchor ripple for each group member used to push the same
    /// followers several times, causing the random-looking timeline jumble.
    fn ripple_lane_after_group(
        &mut self,
        track: TrackId,
        moving_ids: &HashSet<String>,
    ) -> Result<(), TimelineError> {
        let anchors: Vec<Clip> = self.clips.iter()
            .filter(|clip| clip.track == track && moving_ids.contains(&clip.id)
                && !clip_is_subtitle(clip))
            .cloned().collect();
        if anchors.is_empty() { return Ok(()); }
        let first_start = anchors.iter().map(|clip| clip.start).fold(f64::INFINITY, f64::min);
        let mut cursor = anchors.iter().map(Clip::end).fold(first_start, f64::max);

        let mut followers: Vec<Clip> = self.clips.iter()
            .filter(|clip| clip.track == track && !clip_is_subtitle(clip)
                && !moving_ids.contains(&clip.id) && clip.end() > first_start + EPSILON)
            .cloned().collect();
        followers.sort_by(|a, b| a.start.total_cmp(&b.start).then_with(|| a.id.cmp(&b.id)));

        for mut clip in followers {
            let old_start = clip.start;
            /* A magnetic row contains no internal blank spaces. Push overlaps
               forward and pull pre-existing gaps closed after the inserted edit. */
            clip.start = cursor;
            let delta = clip.start - old_start;
            if delta.abs() > EPSILON {
                shift_clip_keyframes(&mut clip, delta);
                let linked_id = clip.linked_clip_id.clone();
                self.replace(clip.clone());
                if let Some(linked_id) = linked_id {
                    if !moving_ids.contains(&linked_id) {
                        if let Some(mut linked) = self.clips.iter()
                            .find(|item| item.id == linked_id && item.track != track).cloned()
                        {
                            linked.start = (linked.start + delta).max(0.0);
                            shift_clip_keyframes(&mut linked, delta);
                            self.replace(linked);
                        }
                    }
                }
            }
            cursor = clip.end();
        }
        Ok(())
    }

    fn lane_overlaps(&self, id: &str, track: TrackId, start: f64, duration: f64) -> bool {
        self.clips.iter().any(|c| {
            c.id != id && !clip_is_subtitle(c) && c.track == track
                && start < c.end() - EPSILON
                && start + duration > c.start + EPSILON
        })
    }

    /// Keep the dropped/moved clip at its chosen time and push any clips it
    /// collides with later on the same lane, preserving their order.
    fn ripple_lane_after(&mut self, anchor_id: &str) -> Result<(), TimelineError> {
        let anchor = self.find(anchor_id)?.clone();
        let group_id = anchor.extra.get("groupId").and_then(serde_json::Value::as_str);
        let mut followers: Vec<_> = self.clips.iter()
            .filter(|c| c.id != anchor_id && !clip_is_subtitle(c)
                && c.track == anchor.track && c.end() > anchor.start + EPSILON
                && (group_id.is_none() || c.extra.get("groupId").and_then(serde_json::Value::as_str) != group_id))
            .cloned().collect();
        followers.sort_by(|a, b| a.start.total_cmp(&b.start).then_with(|| a.id.cmp(&b.id)));
        let mut cursor = anchor.end();
        for mut clip in followers {
            let previous_start = clip.start;
            if clip.start < cursor - EPSILON { clip.start = cursor; }
            let delta = clip.start - previous_start;
            shift_clip_keyframes(&mut clip, delta);
            if delta.abs() > EPSILON {
                if let Some(linked_id) = clip.linked_clip_id.as_deref() {
                    if let Some(mut linked) = self.clips.iter().find(|item| item.id == linked_id).cloned() {
                        let previous_linked_start = linked.start;
                        linked.start = (linked.start + delta).max(0.0);
                        let linked_delta = linked.start - previous_linked_start;
                        shift_clip_keyframes(&mut linked, linked_delta);
                        self.replace(linked);
                    }
                }
            }
            cursor = clip.end();
            self.replace(clip);
        }
        Ok(())
    }


    fn find(&self, id: &str) -> Result<&Clip, TimelineError> {
        self.clips
            .iter()
            .find(|clip| clip.id == id)
            .ok_or_else(|| TimelineError::ClipNotFound(id.into()))
    }

    fn replace(&mut self, clip: Clip) {
        if let Some(index) = self.clips.iter().position(|current| current.id == clip.id) {
            self.clips[index] = clip;
        }
    }

    fn validate_all(&self) -> Result<(), TimelineError> {
        let mut ids = HashSet::new();
        for clip in &self.clips {
            clip.validate()?;
            if !ids.insert(&clip.id) {
                return Err(TimelineError::DuplicateId(clip.id.clone()));
            }
        }
        Ok(())
    }

    fn refresh_duration(&mut self) {
        self.duration_seconds = self
            .clips
            .iter()
            .map(Clip::end)
            .fold(MIN_TIMELINE_SECONDS, f64::max);
    }
}


#[cfg(test)]
mod tests {
    use super::*;

    fn video_clip(id: &str, start: f64, duration: f64) -> Clip {
        Clip {
            id: id.to_owned(), track: TrackId::V1, start, duration,
            label: id.to_owned(), linked_clip_id: None, extra: HashMap::new(),
        }
    }

    fn subtitle_clip(id: &str, start: f64, duration: f64) -> Clip {
        Clip {
            id: id.to_owned(), track: TrackId::V6, start, duration,
            label: "Subtitle".into(), linked_clip_id: None,
            extra: HashMap::from([(
                "props".into(), serde_json::json!({"text": {"isSubtitle": true, "sourceClipId": "source-video"}}),
            )]),
        }
    }


    #[test]
    fn timeline_starts_with_only_the_primary_video_track_visible() {
        let timeline = Timeline::default();
        assert_eq!(timeline.visible_tracks(), HashSet::from([TrackId::V1]));
    }

    #[test]
    fn clips_can_be_added_to_the_new_v5_and_v6_video_lanes() {
        let mut timeline = Timeline::default();
        timeline.snapping = false;
        timeline.magnetic = false;

        let mut v5 = video_clip("overlay-v5", 0.0, 2.0);
        v5.track = TrackId::V5;
        let mut v6 = video_clip("overlay-v6", 0.0, 2.0);
        v6.track = TrackId::V6;

        timeline.add_clip(v5).unwrap();
        timeline.add_clip(v6).unwrap();

        assert_eq!(timeline.clips.iter().find(|clip| clip.id == "overlay-v5").unwrap().track, TrackId::V5);
        assert_eq!(timeline.clips.iter().find(|clip| clip.id == "overlay-v6").unwrap().track, TrackId::V6);
        assert!(timeline.visible_tracks().contains(&TrackId::V5));
        assert!(timeline.visible_tracks().contains(&TrackId::V6));
    }

    #[test]
    fn subtitle_clips_keep_exact_times_and_can_overlap_on_the_s_lane() {
        let mut timeline = Timeline::default();
        let first = 12.345678;
        let second = 12.800123;
        timeline.add_clip(subtitle_clip("subtitle-one", first, 1.2)).unwrap();
        timeline.add_clip(subtitle_clip("subtitle-two", second, 0.8)).unwrap();
        let one = timeline.clips.iter().find(|clip| clip.id == "subtitle-one").unwrap();
        let two = timeline.clips.iter().find(|clip| clip.id == "subtitle-two").unwrap();
        assert_eq!(one.track, TrackId::V6);
        assert_eq!(two.track, TrackId::V6);
        assert!((one.start - first).abs() < 1e-9);
        assert!((two.start - second).abs() < 1e-9);
        assert_eq!(timeline.visible_tracks(), HashSet::from([TrackId::V1]));
    }

    #[test]
    fn ordinary_v6_clips_can_move_across_subtitles_without_snapping_to_them() {
        let mut timeline = Timeline::default();
        timeline.snapping = false;
        timeline.magnetic = true;
        timeline.add_clip(subtitle_clip("subtitle", 4.0, 2.0)).unwrap();
        let mut video = video_clip("v6-video", 0.0, 1.0);
        video.track = TrackId::V6;
        timeline.add_clip(video).unwrap();
        let moved = timeline.move_clip("v6-video", 4.0, TrackId::V6, 0.0, 0.25).unwrap();
        assert_eq!(moved.track, TrackId::V6);
        assert!((moved.start - 4.0).abs() < 1e-9);
    }

    #[test]
    fn subtitles_do_not_occupy_or_ripple_their_backing_video_track() {
        let mut timeline = Timeline::default();
        timeline.add_clip(subtitle_clip("subtitle", 0.0, 2.0)).unwrap();
        let mut video = video_clip("v6-video", 0.0, 2.0);
        video.track = TrackId::V6;
        timeline.add_clip(video).unwrap();
        let before_delete = timeline.clips.iter().find(|clip| clip.id == "v6-video").unwrap();
        assert_eq!(before_delete.track, TrackId::V6, "a caption is not a competing video clip");
        assert!((before_delete.start - 0.0).abs() < 1e-9);
        timeline.delete_clip("subtitle").unwrap();
        let survivor = timeline.clips.iter().find(|clip| clip.id == "v6-video").unwrap();
        assert!((survivor.start - 0.0).abs() < 1e-9);
    }

    #[test]
    fn trimming_subtitles_ignores_the_backing_video_lane_and_keeps_absolute_times() {
        let mut timeline = Timeline::default();
        timeline.snapping = false;
        timeline.magnetic = false;
        timeline.add_clip(subtitle_clip("subtitle", 2.0, 4.0)).unwrap();
        let mut video = video_clip("v6-video", 2.0, 4.0);
        video.track = TrackId::V6;
        timeline.add_clip(video).unwrap();

        let trimmed = timeline.trim_clip("subtitle", 3.0, 5.0, None).unwrap();
        let video = timeline.clips.iter().find(|clip| clip.id == "v6-video").unwrap();
        assert!((trimmed.start - 3.0).abs() < 1e-9);
        assert!((trimmed.duration - 2.0).abs() < 1e-9);
        assert!((video.start - 2.0).abs() < 1e-9);
        assert!((video.duration - 4.0).abs() < 1e-9);
    }

    #[test]
    fn all_six_video_track_ids_round_trip_and_sort_below_audio_tracks() {
        for (name, track, index) in [
            ("v1", TrackId::V1, 0), ("v2", TrackId::V2, 1),
            ("v3", TrackId::V3, 2), ("v4", TrackId::V4, 3),
            ("v5", TrackId::V5, 4), ("v6", TrackId::V6, 5),
        ] {
            assert!(track.is_video());
            assert_eq!(serde_json::from_str::<TrackId>(&format!("\"{name}\"" )).unwrap(), track);
            assert_eq!(serde_json::to_string(&track).unwrap(), format!("\"{name}\""));
            assert_eq!(track.sort_index(), index);
        }
        assert_eq!(TrackId::A1.sort_index(), 6);
        assert_eq!(TrackId::A6.sort_index(), 11);
        assert!(!TrackId::A1.is_video());
    }

    #[test]
    fn all_six_audio_track_ids_round_trip_through_project_json() {
        for (name, track) in [
            ("a1", TrackId::A1), ("a2", TrackId::A2), ("a3", TrackId::A3),
            ("a4", TrackId::A4), ("a5", TrackId::A5), ("a6", TrackId::A6),
        ] {
            assert_eq!(serde_json::from_str::<TrackId>(&format!("\"{name}\"" )).unwrap(), track);
            assert_eq!(serde_json::to_string(&track).unwrap(), format!("\"{name}\""));
        }
    }

    #[test]
    fn separated_audio_uses_the_next_free_audio_layer_when_a1_is_occupied() {
        let mut video = video_clip("video", 1.0, 3.0);
        video.extra.insert("hasAudio".into(), serde_json::json!(true));
        let occupying_audio = Clip {
            id: "music".into(), track: TrackId::A1, start: 1.0, duration: 3.0,
            label: "Music".into(), linked_clip_id: None,
            extra: HashMap::from([("kind".into(), serde_json::json!("audio"))]),
        };
        let mut timeline = Timeline::default();
        timeline.clips.extend([video, occupying_audio]);
        timeline.snapping = false;

        let separated = timeline.separate_audio("video", "video-audio".into()).unwrap();
        assert_eq!(separated.track, TrackId::A2);
        assert_eq!(separated.linked_clip_id.as_deref(), Some("video"));
    }

    #[test]
    fn separately_placed_audio_can_be_linked_to_its_matching_video_clip() {
        let mut video = video_clip("video", 1.0, 4.0);
        video.extra.insert("sourceOffset".into(), serde_json::json!(1.5));
        let audio = Clip {
            id: "audio".into(), track: TrackId::A1, start: 1.0, duration: 4.0,
            label: "audio".into(), linked_clip_id: None,
            extra: HashMap::from([
                ("kind".into(), serde_json::json!("audio")),
                ("sourceOffset".into(), serde_json::json!(0.5)),
            ]),
        };
        let mut timeline = Timeline::default();
        timeline.clips.extend([video, audio]);
        timeline.link_clips("video", "audio").unwrap();
        assert_eq!(timeline.clips.iter().find(|clip| clip.id == "video").unwrap().linked_clip_id.as_deref(), Some("audio"));
        assert_eq!(timeline.clips.iter().find(|clip| clip.id == "audio").unwrap().linked_clip_id.as_deref(), Some("video"));
        timeline.unlink_clip("audio").unwrap();
        assert_eq!(timeline.find("video").unwrap().extra.get("unlinkedFromClipId").and_then(serde_json::Value::as_str), Some("audio"));
        assert_eq!(timeline.find("audio").unwrap().extra.get("unlinkedFromClipId").and_then(serde_json::Value::as_str), Some("video"));
        timeline.link_clips("video", "audio").unwrap();
        assert_eq!(timeline.find("video").unwrap().extra.get("unlinkedFromClipId"), None);
        assert_eq!(timeline.clips.iter().find(|clip| clip.id == "video").unwrap().linked_clip_id.as_deref(), Some("audio"));
    }

    #[test]
    fn linked_external_audio_keeps_its_own_source_offset_when_video_is_trimmed() {
        let mut video = video_clip("video", 1.0, 4.0);
        video.extra.insert("sourceOffset".into(), serde_json::json!(2.0));
        video.extra.insert("sourceDuration".into(), serde_json::json!(12.0));
        let audio = Clip {
            id: "music-audio".into(), track: TrackId::A1, start: 1.0, duration: 4.0,
            label: "Music".into(), linked_clip_id: None,
            extra: HashMap::from([
                ("kind".into(), serde_json::json!("audio")),
                ("sourceOffset".into(), serde_json::json!(0.5)),
                ("sourceDuration".into(), serde_json::json!(8.0)),
            ]),
        };
        let mut timeline = Timeline::default();
        timeline.clips.extend([video, audio]);
        timeline.link_clips("video", "music-audio").unwrap();
        timeline.trim_clip("video", 2.0, 4.5, None).unwrap();
        let video_after = timeline.find("video").unwrap();
        let audio_after = timeline.find("music-audio").unwrap();
        assert_eq!(video_after.extra.get("sourceOffset").and_then(serde_json::Value::as_f64), Some(3.0));
        assert_eq!(audio_after.extra.get("sourceOffset").and_then(serde_json::Value::as_f64), Some(1.5));
        assert_eq!(audio_after.extra.get("sourceDuration").and_then(serde_json::Value::as_f64), Some(8.0));
        assert_eq!(video_after.duration, audio_after.duration);
    }

    #[test]
    fn detached_audio_can_be_reattached_after_unlinking() {
        let mut video = video_clip("video", 1.0, 4.0);
        video.extra.insert("hasAudio".into(), serde_json::json!(true));
        video.extra.insert("path".into(), serde_json::json!("same.mp4"));
        video.extra.insert("sourceOffset".into(), serde_json::json!(0.5));
        let mut timeline = Timeline::default();
        timeline.clips.push(video);
        timeline.separate_audio("video", "video-audio".into()).unwrap();
        timeline.unlink_clip("video-audio").unwrap();
        timeline.reattach_audio("video", "video-audio").unwrap();
        assert_eq!(timeline.clips.len(), 1);
        let restored = timeline.find("video").unwrap();
        assert_eq!(restored.extra.get("hasAudio").and_then(serde_json::Value::as_bool), Some(true));
        assert_eq!(restored.linked_clip_id, None);
    }

    #[test]
    fn split_detached_audio_keeps_the_matching_video_origin_for_reattach() {
        let mut video = video_clip("video", 1.0, 4.0);
        video.extra.insert("hasAudio".into(), serde_json::json!(true));
        video.extra.insert("path".into(), serde_json::json!("same.mp4"));
        let mut timeline = Timeline::default();
        timeline.clips.push(video);
        timeline.separate_audio("video", "video-audio".into()).unwrap();
        timeline.split_clip("video", 3.0, "video-right".into()).unwrap();
        timeline.reattach_audio("video-right", "video-right-linked").unwrap();
        assert!(timeline.find("video-right-linked").is_err());
        assert_eq!(timeline.find("video-right").unwrap().extra.get("hasAudio").and_then(serde_json::Value::as_bool), Some(true));
    }

    #[test]
    fn splitting_linked_video_and_audio_keeps_both_halves_reciprocally_linked() {
        let mut video = video_clip("video", 1.0, 6.0);
        video.extra.insert("kind".into(), serde_json::json!("video"));
        video.extra.insert("sourceOffset".into(), serde_json::json!(2.0));
        video.linked_clip_id = Some("audio".into());
        let mut audio = Clip {
            id: "audio".into(), track: TrackId::A1, start: 1.0, duration: 6.0,
            label: "audio".into(), linked_clip_id: Some("video".into()),
            extra: HashMap::from([
                ("kind".into(), serde_json::json!("audio")),
                ("sourceOffset".into(), serde_json::json!(2.0)),
            ]),
        };
        audio.extra.insert("sourceDuration".into(), serde_json::json!(20.0));
        let mut timeline = Timeline::default();
        timeline.clips.extend([video, audio]);

        timeline.split_clip("video", 4.0, "video-right".into()).unwrap();

        let video_left = timeline.clips.iter().find(|clip| clip.id == "video").unwrap();
        let video_right = timeline.clips.iter().find(|clip| clip.id == "video-right").unwrap();
        let audio_left = timeline.clips.iter().find(|clip| clip.id == "audio").unwrap();
        let audio_right = timeline.clips.iter().find(|clip| clip.id == "video-right-linked").unwrap();
        assert_eq!(video_left.linked_clip_id.as_deref(), Some("audio"));
        assert_eq!(audio_left.linked_clip_id.as_deref(), Some("video"));
        assert_eq!(video_right.linked_clip_id.as_deref(), Some("video-right-linked"));
        assert_eq!(audio_right.linked_clip_id.as_deref(), Some("video-right"));
        assert!((video_left.duration - 3.0).abs() < 1e-9);
        assert!((video_right.start - 4.0).abs() < 1e-9);
        assert!((video_right.extra["sourceOffset"].as_f64().unwrap() - 5.0).abs() < 1e-9);
        assert!((audio_right.extra["sourceOffset"].as_f64().unwrap() - 5.0).abs() < 1e-9);
    }

    #[test]
    fn trim_of_linked_audio_keeps_video_time_and_source_offsets_identical() {
        let mut video = video_clip("video", 1.0, 6.0);
        video.linked_clip_id = Some("audio".into());
        video.extra.insert("sourceOffset".into(), serde_json::json!(2.0));
        video.extra.insert("sourceDuration".into(), serde_json::json!(20.0));
        let audio = Clip {
            id: "audio".into(), track: TrackId::A1, start: 1.0, duration: 6.0,
            label: "audio".into(), linked_clip_id: Some("video".into()),
            extra: HashMap::from([
                ("kind".into(), serde_json::json!("audio")),
                ("sourceOffset".into(), serde_json::json!(2.0)),
                ("sourceDuration".into(), serde_json::json!(20.0)),
            ]),
        };
        let mut timeline = Timeline::default();
        timeline.clips.extend([video, audio]);
        timeline.trim_clip("audio", 2.0, 6.0, None).unwrap();
        let video = timeline.clips.iter().find(|clip| clip.id == "video").unwrap();
        let audio = timeline.clips.iter().find(|clip| clip.id == "audio").unwrap();
        assert!((video.start - 2.0).abs() < 1e-9);
        assert!((video.duration - 4.0).abs() < 1e-9);
        assert!((video.extra["sourceOffset"].as_f64().unwrap() - 3.0).abs() < 1e-9);
        assert!((audio.start - video.start).abs() < 1e-9);
        assert!((audio.duration - video.duration).abs() < 1e-9);
        assert!((audio.extra["sourceOffset"].as_f64().unwrap() - 3.0).abs() < 1e-9);
    }

    #[test]
    fn separated_audio_keeps_video_timing_even_when_audio_lane_has_other_clips() {
        let mut timeline = Timeline::default();
        let mut video = video_clip("source-video", 3.0, 2.0);
        video.extra.insert("hasAudio".into(), serde_json::json!(true));
        video.extra.insert("kind".into(), serde_json::json!("video"));
        let mut existing_audio = video_clip("existing-audio", 10.0, 3.0);
        existing_audio.track = TrackId::A1;
        existing_audio.extra.insert("kind".into(), serde_json::json!("audio"));
        timeline.clips.extend([video, existing_audio]);
        let separated = timeline.separate_audio("source-video", "source-audio".into()).unwrap();
        assert_eq!(separated.track, TrackId::A1);
        assert!((separated.start - 3.0).abs() < 1e-9,
            "separated audio must remain aligned with its source video instead of magnetizing to another A1 edit");
    }

    #[test]
    fn relink_rejects_media_that_does_not_match_the_track_type() {
        let mut timeline = Timeline::default();
        let mut audio = video_clip("audio", 0.0, 3.0);
        audio.track = TrackId::A1;
        audio.extra.insert("kind".into(), serde_json::json!("audio"));
        timeline.clips.push(audio);
        assert!(timeline.relink_clip("audio", "video.mp4", "Video", 10.0, "video", true).is_err());
        let relinked = timeline.relink_clip("audio", "replacement.wav", "Replacement", 8.0, "audio", false).unwrap();
        assert_eq!(relinked.extra["path"].as_str(), Some("replacement.wav"));
        assert_eq!(relinked.label, "Replacement");
    }

    #[test]
    fn move_clip_is_free_when_proximity_snapping_is_disabled() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("one", 0.0, 2.0));
        timeline.snapping = false;
        let moved = timeline.move_clip("one", 5.37, TrackId::V1, 0.0, 0.25).unwrap();
        assert!((moved.start - 5.37).abs() < 1e-9);
    }

    #[test]
    fn moving_clip_keeps_absolute_keyframes_attached_to_the_clip() {
        let mut clip = video_clip("animated", 2.0, 4.0);
        clip.extra.insert("keyframes".into(), serde_json::json!([
            {"time": 2.25, "values": {"x": 0.0}},
            {"time": 4.0, "values": {"x": 100.0}}
        ]));
        let mut timeline = Timeline::default();
        timeline.clips.push(clip);
        timeline.snapping = false;

        timeline.move_clip("animated", 7.0, TrackId::V1, 0.0, 0.0).unwrap();
        let moved = timeline.clips.iter().find(|clip| clip.id == "animated").unwrap();
        let frames = moved.extra["keyframes"].as_array().unwrap();
        assert!((moved.start - 7.0).abs() < 1e-9);
        assert!((frames[0]["time"].as_f64().unwrap() - 7.25).abs() < 1e-9);
        assert!((frames[1]["time"].as_f64().unwrap() - 9.0).abs() < 1e-9);
    }

    #[test]
    fn moving_video_shifts_linked_audio_keyframes_by_the_same_amount() {
        let mut video = video_clip("video", 2.0, 4.0);
        video.linked_clip_id = Some("audio".into());
        let mut audio = Clip {
            id: "audio".into(), track: TrackId::A1, start: 2.0, duration: 4.0,
            label: "video Audio".into(), linked_clip_id: Some("video".into()), extra: HashMap::new(),
        };
        audio.extra.insert("keyframes".into(), serde_json::json!([
            {"time": 2.5, "values": {"volume": 0.0}},
            {"time": 5.5, "values": {"volume": 100.0}}
        ]));
        let mut timeline = Timeline::default();
        timeline.clips.extend([video, audio]);
        timeline.snapping = false;

        timeline.move_clip("video", 7.0, TrackId::V1, 0.0, 0.0).unwrap();
        let video = timeline.clips.iter().find(|clip| clip.id == "video").unwrap();
        let audio = timeline.clips.iter().find(|clip| clip.id == "audio").unwrap();
        assert!((video.start - 7.0).abs() < 1e-9);
        assert!((audio.start - 7.0).abs() < 1e-9);
        assert!((audio.extra["keyframes"][0]["time"].as_f64().unwrap() - 7.5).abs() < 1e-9);
        assert!((audio.extra["keyframes"][1]["time"].as_f64().unwrap() - 10.5).abs() < 1e-9);
    }

    #[test]
    fn moving_a_clip_closes_the_shared_video_gap_without_shifting_audio() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-anchor", 0.0, 2.0));
        timeline.clips.push(video_clip("moving", 2.0, 2.0));
        timeline.clips.push(video_clip("v1-follower", 4.0, 2.0));
        let mut upper = video_clip("v2-overlay", 6.0, 2.0);
        upper.track = TrackId::V2;
        timeline.clips.push(upper);
        let mut audio = video_clip("audio-follower", 6.0, 2.0);
        audio.track = TrackId::A1;
        timeline.clips.push(audio);
        timeline.snapping = false;
        timeline.magnetic = true;

        /* Moving V1 content to V3 closes its vacated interval across the shared
           video sequence, while independent audio timing remains untouched. */
        timeline.move_clip("moving", 10.0, TrackId::V3, 0.0, 0.25).unwrap();
        let moved = timeline.clips.iter().find(|clip| clip.id == "moving").unwrap();
        let v1_follower = timeline.clips.iter().find(|clip| clip.id == "v1-follower").unwrap();
        let upper = timeline.clips.iter().find(|clip| clip.id == "v2-overlay").unwrap();
        let audio = timeline.clips.iter().find(|clip| clip.id == "audio-follower").unwrap();
        assert_eq!(moved.track, TrackId::V3);
        assert!((moved.start - 6.0).abs() < 1e-9,
            "a moved clip joins the end of the shared magnetic video sequence");
        assert!((v1_follower.start - 2.0).abs() < 1e-9, "the vacated global video gap should close");
        assert!((upper.start - 4.0).abs() < 1e-9, "the shared video sequence closes the vacated interval across rows");
        assert!((audio.start - 6.0).abs() < 1e-9, "audio must not be rippled by a video move");
    }

    #[test]
    fn deleting_a_video_clip_closes_the_shared_video_sequence() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("before", 0.0, 2.0));
        timeline.clips.push(video_clip("remove", 2.0, 3.0));
        timeline.clips.push(video_clip("same-lane-follower", 7.0, 2.0));
        let mut upper = video_clip("upper", 7.0, 2.0);
        upper.track = TrackId::V3;
        timeline.clips.push(upper);
        let mut audio = video_clip("music", 7.0, 2.0);
        audio.track = TrackId::A2;
        timeline.clips.push(audio);
        timeline.delete_clip("remove").unwrap();
        assert!((timeline.clips.iter().find(|clip| clip.id == "same-lane-follower").unwrap().start - 4.0).abs() < 1e-9);
        assert!((timeline.clips.iter().find(|clip| clip.id == "upper").unwrap().start - 4.0).abs() < 1e-9);
        assert!((timeline.clips.iter().find(|clip| clip.id == "music").unwrap().start - 7.0).abs() < 1e-9);
    }

    #[test]
    fn video_tracks_share_one_magnetic_sequence_but_keep_intentional_overlaps() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-main", 0.0, 4.0));
        let mut overlay = video_clip("v2-overlay", 1.0, 2.0);
        overlay.track = TrackId::V2;
        timeline.clips.push(overlay);
        let mut later = video_clip("v3-later", 9.0, 2.0);
        later.track = TrackId::V3;
        timeline.clips.push(later);
        timeline.snapping = false;
        timeline.magnetic = true;

        timeline.compact_lane(TrackId::V1, &HashSet::new());
        let find = |id: &str| timeline.clips.iter().find(|clip| clip.id == id).unwrap();
        assert!((find("v2-overlay").start - 1.0).abs() < 1e-9,
            "cross-track overlap is an intentional overlay and must remain untouched");
        assert!((find("v3-later").start - 4.0).abs() < 1e-9,
            "a later clip on another video row closes the global uncovered gap");
    }

    #[test]
    fn dropping_inside_another_video_row_creates_an_intentional_overlay() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-base", 0.0, 5.0));
        timeline.snapping = false;
        let mut overlay = video_clip("v2-overlay", 1.5, 2.0);
        overlay.track = TrackId::V2;
        timeline.add_clip(overlay).unwrap();
        let overlay = timeline.clips.iter().find(|clip| clip.id == "v2-overlay").unwrap();
        assert!((overlay.start - 1.5).abs() < 1e-9,
            "a drop inside another video row is an explicit overlay, not a magnetic cut insertion");
    }

    #[test]
    fn adding_to_any_video_track_joins_the_shared_sequence() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-first", 0.0, 3.0));
        let mut second = video_clip("v2-second", 3.0, 2.0);
        second.track = TrackId::V2;
        timeline.clips.push(second);
        timeline.snapping = false;
        let mut added = video_clip("v4-added", 30.0, 1.0);
        added.track = TrackId::V4;
        timeline.add_clip(added).unwrap();
        let added = timeline.clips.iter().find(|clip| clip.id == "v4-added").unwrap();
        assert!((added.start - 5.0).abs() < 1e-9,
            "new video clips join the end of the global video sequence regardless of row");
        assert_eq!(added.track, TrackId::V4, "magnetic placement must not change the chosen row");
    }

    #[test]
    fn legacy_timeline_json_defaults_magnetic_mode_on() {
        let value = serde_json::json!({"clips": [], "durationSeconds": 60.0, "snapping": false});
        let timeline: Timeline = serde_json::from_value(value).unwrap();
        assert!(!timeline.snapping);
        assert!(timeline.magnetic);
    }

    #[test]
    fn magnetic_placement_is_independent_of_proximity_snapping_when_adding_video() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("one", 0.0, 2.0));
        timeline.snapping = false;
        let mut added_clip = video_clip("two", 5.37, 1.0);
        added_clip.extra.insert("keyframes".into(), serde_json::json!([
            {"time": 5.5, "values": {"x": 25.0}}
        ]));
        timeline.add_clip(added_clip).unwrap();
        let added = timeline.clips.iter().find(|clip| clip.id == "two").unwrap();
        assert!((added.start - 2.0).abs() < 1e-9, "magnetic placement aligns to the nearest video edge even with snapping off");
        assert!((added.extra["keyframes"][0]["time"].as_f64().unwrap() - 2.13).abs() < 1e-9,
            "absolute keyframe timestamps must follow the magnetic placement adjustment");
    }

    #[test]
    fn magnetic_placement_uses_the_shared_video_sequence_not_audio_splits() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-left", 0.0, 3.0));
        timeline.clips.push(video_clip("v1-right", 3.0, 2.0));
        let mut upper = video_clip("v2-existing", 6.0, 2.0);
        upper.track = TrackId::V2;
        timeline.clips.push(upper);
        let mut audio = video_clip("music", 30.0, 4.0);
        audio.track = TrackId::A1;
        timeline.clips.push(audio);
        timeline.snapping = false;

        let mut added = video_clip("new-v2", 40.0, 1.0);
        added.track = TrackId::V2;
        timeline.add_clip(added).unwrap();
        let added = timeline.clips.iter().find(|clip| clip.id == "new-v2").unwrap();
        assert_eq!(added.track, TrackId::V2);
        assert!((added.start - 7.0).abs() < 1e-9,
            "V2 insertion should attach to the end of the compacted shared video sequence, not audio");
    }

    #[test]
    fn proximity_snap_can_align_a_clip_to_a_nearby_video_cut() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-edge", 0.0, 2.0));
        let mut v2 = video_clip("v2-existing", 10.0, 2.0);
        v2.track = TrackId::V2;
        timeline.clips.push(v2);

        let mut near = video_clip("v2-near-edge", 1.9, 1.0);
        near.track = TrackId::V2;
        timeline.add_clip(near).unwrap();
        let near = timeline.clips.iter().find(|clip| clip.id == "v2-near-edge").unwrap();
        assert!((near.start - 2.0).abs() < 1e-9,
            "a nearby edge on another lane should be a normal proximity snap target");
    }

    #[test]
    fn cross_row_proximity_snap_must_land_on_a_shared_video_cut() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-edge", 3.0, 1.0));
        let mut v2_existing = video_clip("v2-existing", 0.0, 2.0);
        v2_existing.track = TrackId::V2;
        timeline.clips.push(v2_existing);

        let mut added = video_clip("v2-added", 3.1, 1.0);
        added.track = TrackId::V2;
        timeline.add_clip(added).unwrap();
        let added = timeline.clips.iter().find(|clip| clip.id == "v2-added").unwrap();
        let existing = timeline.clips.iter().find(|clip| clip.id == "v2-existing").unwrap();
        assert!((added.start - 3.0).abs() < 1e-9,
            "the shared video sequence uses the nearest valid global cut after compaction");
        assert!((existing.start - 0.0).abs() < 1e-9);
    }

    #[test]
    fn distant_video_insertion_joins_the_end_of_the_shared_sequence() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-left", 0.0, 3.0));
        timeline.clips.push(video_clip("v1-right", 3.0, 2.0));
        let mut added = video_clip("v2-at-pointer", 40.0, 1.0);
        added.track = TrackId::V2;
        timeline.add_clip(added).unwrap();
        let added = timeline.clips.iter().find(|clip| clip.id == "v2-at-pointer").unwrap();
        assert!((added.start - 5.0).abs() < 1e-9,
            "a video insertion joins the end of the shared V1-V6 sequence");
    }

    #[test]
    fn magnetic_audio_tracks_are_independent_and_close_their_own_gaps() {
        let mut timeline = Timeline::default();
        let mut first = video_clip("a1-first", 0.0, 2.0);
        first.track = TrackId::A1;
        first.extra.insert("kind".into(), serde_json::json!("audio"));
        let mut remove = video_clip("a1-remove", 2.0, 3.0);
        remove.track = TrackId::A1;
        remove.extra.insert("kind".into(), serde_json::json!("audio"));
        let mut follower = video_clip("a1-follower", 7.0, 2.0);
        follower.track = TrackId::A1;
        follower.extra.insert("kind".into(), serde_json::json!("audio"));
        let mut other = video_clip("a2-other", 7.0, 2.0);
        other.track = TrackId::A2;
        other.extra.insert("kind".into(), serde_json::json!("audio"));
        timeline.clips.extend([first, remove, follower, other]);

        timeline.delete_clip("a1-remove").unwrap();
        assert!((timeline.clips.iter().find(|clip| clip.id == "a1-follower").unwrap().start - 4.0).abs() < 1e-9);
        assert!((timeline.clips.iter().find(|clip| clip.id == "a2-other").unwrap().start - 7.0).abs() < 1e-9);
    }

    #[test]
    fn deleting_v2_clip_closes_the_shared_video_gap_across_rows() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-left", 0.0, 3.0));
        timeline.clips.push(video_clip("v1-right", 3.0, 3.0));
        let mut v2_before = video_clip("v2-before", 0.0, 2.0);
        v2_before.track = TrackId::V2;
        let mut v2_remove = video_clip("v2-remove", 2.0, 3.0);
        v2_remove.track = TrackId::V2;
        let mut v2_after = video_clip("v2-after", 5.0, 2.0);
        v2_after.track = TrackId::V2;
        timeline.clips.extend([v2_before, v2_remove, v2_after]);

        timeline.delete_clip("v2-remove").unwrap();
        let find = |id: &str| timeline.clips.iter().find(|clip| clip.id == id).unwrap();
        assert!((find("v2-after").start - 5.0).abs() < 1e-9,
            "deleting an upper-row clip must not remove time still covered by V1");
        assert!((find("v1-left").start - 0.0).abs() < 1e-9);
        assert!((find("v1-right").start - 3.0).abs() < 1e-9,
            "uncovered global gaps close without disturbing the existing V1 cut");
    }

    #[test]
    fn moving_a_clip_compacts_both_the_vacated_and_destination_rows() {
        let mut timeline = Timeline::default();
        let mut source_a = video_clip("v2-a", 0.0, 2.0);
        source_a.track = TrackId::V2;
        let mut moving = video_clip("v2-moving", 2.0, 2.0);
        moving.track = TrackId::V2;
        let mut source_b = video_clip("v2-b", 8.0, 2.0);
        source_b.track = TrackId::V2;
        let mut destination_a = video_clip("v3-a", 0.0, 2.0);
        destination_a.track = TrackId::V3;
        let mut destination_b = video_clip("v3-b", 9.0, 2.0);
        destination_b.track = TrackId::V3;
        timeline.clips.extend([source_a, moving, source_b, destination_a, destination_b]);
        timeline.snapping = false;

        timeline.move_clip("v2-moving", 2.0, TrackId::V3, 0.0, 0.0).unwrap();
        let find = |id: &str| timeline.clips.iter().find(|clip| clip.id == id).unwrap();
        assert!((find("v2-b").start - 2.0).abs() < 1e-9,
            "the shared video sequence closes the vacated interval across rows");
        assert!((find("v3-b").start - 4.0).abs() < 1e-9,
            "the destination row remains correctly ordered after insertion");
    }

    #[test]
    fn title_insertion_joins_the_shared_video_sequence_on_v6() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-a", 0.0, 4.0));
        let mut v6_a = video_clip("v6-a", 0.0, 2.0);
        v6_a.track = TrackId::V6;
        let mut v6_b = video_clip("v6-b", 6.0, 2.0);
        v6_b.track = TrackId::V6;
        timeline.clips.extend([v6_a, v6_b]);

        let mut title = video_clip("title-gap-insert", 2.0, 1.0);
        title.track = TrackId::V6;
        title.extra.insert("props".into(), serde_json::json!({"text": {"isTitle": true}}));
        timeline.add_clip(title).unwrap();
        let find = |id: &str| timeline.clips.iter().find(|clip| clip.id == id).unwrap();
        assert_eq!(find("title-gap-insert").track, TrackId::V6);
        assert!((find("title-gap-insert").start - 2.0).abs() < 1e-9);
        assert!((find("v6-b").start - 4.0).abs() < 1e-9,
            "text insertion joins the shared video sequence without destroying overlays");
        assert!((find("v1-a").start - 0.0).abs() < 1e-9,
            "title insertion must not change V1");
    }

    #[test]
    fn title_clips_are_placed_on_the_text_lane_without_jumping_to_another_track() {
        let mut timeline = Timeline::default();
        let mut existing = video_clip("v6-existing", 0.0, 2.0);
        existing.track = TrackId::V6;
        timeline.clips.push(existing);
        let mut title = video_clip("title", 40.0, 3.0);
        title.track = TrackId::V6;
        title.extra.insert("props".into(), serde_json::json!({"text": {"isTitle": true}}));
        timeline.add_clip(title).unwrap();
        let title = timeline.clips.iter().find(|clip| clip.id == "title").unwrap();
        assert_eq!(title.track, TrackId::V6);
        assert!((title.start - 2.0).abs() < 1e-9,
            "a title on V6 should magnetically attach to V6, not be reassigned or pulled by V1");
    }

    #[test]
    fn title_placement_on_v6_ignores_splits_on_lower_video_tracks() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-left", 0.0, 3.0));
        timeline.clips.push(video_clip("v1-right", 3.0, 2.0));
        let mut v2 = video_clip("v2-overlay", 5.0, 2.0);
        v2.track = TrackId::V2;
        timeline.clips.push(v2);

        let mut title = video_clip("title-at-playhead", 7.1, 4.0);
        title.track = TrackId::V6;
        title.extra.insert("props".into(), serde_json::json!({"text": {"isTitle": true}}));
        timeline.add_clip(title).unwrap();

        let placed = timeline.clips.iter().find(|clip| clip.id == "title-at-playhead").unwrap();
        assert_eq!(placed.track, TrackId::V6);
        assert!((placed.start - 7.1).abs() < 1e-9,
            "creating text on an empty V6 lane must not magnetize to a nearby V1/V2 cut");
    }

    #[test]
    fn moving_between_video_lanes_changes_only_source_and_destination_sequences() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("v1-a", 0.0, 2.0));
        timeline.clips.push(video_clip("v1-b", 2.0, 2.0));
        let mut moving = video_clip("move-v2", 0.0, 2.0);
        moving.track = TrackId::V2;
        timeline.clips.push(moving);
        let mut v2_follower = video_clip("v2-follow", 2.0, 2.0);
        v2_follower.track = TrackId::V2;
        timeline.clips.push(v2_follower);
        let mut v3_before = video_clip("v3-before", 0.0, 2.0);
        v3_before.track = TrackId::V3;
        timeline.clips.push(v3_before);
        let mut v3_after = video_clip("v3-after", 2.0, 2.0);
        v3_after.track = TrackId::V3;
        timeline.clips.push(v3_after);
        let mut v4 = video_clip("v4-overlay", 0.0, 2.0);
        v4.track = TrackId::V4;
        timeline.clips.push(v4);
        let mut audio = video_clip("a1-audio", 0.0, 2.0);
        audio.track = TrackId::A1;
        audio.extra.insert("kind".into(), serde_json::json!("audio"));
        timeline.clips.push(audio);
        timeline.snapping = false;
        timeline.magnetic = true;

        timeline.move_clip("move-v2", 2.0, TrackId::V3, 0.0, 0.0).unwrap();

        let find = |id: &str| timeline.clips.iter().find(|clip| clip.id == id).unwrap();
        assert_eq!(find("move-v2").track, TrackId::V3);
        assert!((find("move-v2").start - 2.0).abs() < 1e-9);
        assert_eq!(find("v1-a").start, 0.0);
        assert_eq!(find("v1-b").start, 2.0);
        assert!((find("v2-follow").start - 2.0).abs() < 1e-9,
            "a cross-row overlay remains in place when it is still covered by the shared video sequence");
        assert!((find("v3-after").start - 4.0).abs() < 1e-9,
            "only the destination V3 sequence should ripple around the moved clip");
        assert_eq!(find("v4-overlay").start, 0.0);
        assert_eq!(find("a1-audio").start, 0.0);
    }

    #[test]
    fn adding_to_an_occupied_lane_ripples_that_lane_instead_of_changing_tracks() {
        let mut timeline = Timeline::default();
        let mut first = video_clip("v2-first", 0.0, 2.0);
        first.track = TrackId::V2;
        timeline.clips.push(first);
        let mut follower = video_clip("v2-follower", 2.0, 2.0);
        follower.track = TrackId::V2;
        timeline.clips.push(follower);
        let mut added = video_clip("v2-added", 1.5, 2.0);
        added.track = TrackId::V2;
        timeline.add_clip(added).unwrap();
        let added = timeline.clips.iter().find(|clip| clip.id == "v2-added").unwrap();
        let follower = timeline.clips.iter().find(|clip| clip.id == "v2-follower").unwrap();
        assert_eq!(added.track, TrackId::V2);
        assert!((added.start - 2.0).abs() < 1e-9);
        assert!((follower.start - 4.0).abs() < 1e-9,
            "the destination lane should ripple forward rather than auto-assigning V1/V3");
    }

    #[test]
    fn moving_with_magnet_off_does_not_ripple_following_clips() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("one", 0.0, 2.0));
        timeline.clips.push(video_clip("two", 2.0, 3.0));
        timeline.snapping = false;
        timeline.magnetic = false;
        timeline.move_clip("one", 6.25, TrackId::V1, 0.0, 0.25).unwrap();
        let moved = timeline.clips.iter().find(|clip| clip.id == "one").unwrap();
        let follower = timeline.clips.iter().find(|clip| clip.id == "two").unwrap();
        assert!((moved.start - 6.25).abs() < 1e-9, "magnetic-off moves must preserve the requested drop position");
        assert!((follower.start - 2.0).abs() < 1e-9, "magnetic-off moves must preserve the lane gap");
    }

    #[test]
    fn moving_with_magnet_on_closes_the_vacated_lane_gap_and_moves_follower_keyframes() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("one", 0.0, 2.0));
        let mut follower = video_clip("two", 2.0, 3.0);
        follower.extra.insert("keyframes".into(), serde_json::json!([
            {"time": 2.25, "values": {"x": 0.0}},
            {"time": 4.0, "values": {"x": 100.0}}
        ]));
        timeline.clips.push(follower);
        timeline.snapping = true;
        timeline.move_clip("one", 6.25, TrackId::V1, 0.0, 0.0).unwrap();
        let moved = timeline.clips.iter().find(|clip| clip.id == "one").unwrap();
        let follower = timeline.clips.iter().find(|clip| clip.id == "two").unwrap();
        assert!((moved.start - 3.0).abs() < 1e-9,
            "the moved clip should attach to the end of the remaining shared video sequence");
        assert!((follower.start - 0.0).abs() < 1e-9, "magnetic moves should close the vacated gap");
        let frames = follower.extra["keyframes"].as_array().unwrap();
        assert!((frames[0]["time"].as_f64().unwrap() - 0.25).abs() < 1e-9);
        assert!((frames[1]["time"].as_f64().unwrap() - 2.0).abs() < 1e-9);
    }

    #[test]
    fn ripple_pushes_follower_keyframes_with_their_clip() {
        let mut timeline = Timeline::default();
        timeline.clips.push(video_clip("anchor", 0.0, 2.0));
        let mut follower = video_clip("follower", 1.0, 2.0);
        follower.extra.insert("keyframes".into(), serde_json::json!([
            {"time": 1.25, "values": {"x": 0.0}}
        ]));
        timeline.clips.push(follower);
        timeline.ripple_lane_after("anchor").unwrap();
        let follower = timeline.clips.iter().find(|clip| clip.id == "follower").unwrap();
        assert!((follower.start - 2.0).abs() < 1e-9);
        assert!((follower.extra["keyframes"][0]["time"].as_f64().unwrap() - 2.25).abs() < 1e-9);
    }

    #[test]
    fn left_trim_moves_keyframes_with_the_clip_start() {
        let mut clip = video_clip("trimmed", 2.0, 4.0);
        clip.extra.insert("keyframes".into(), serde_json::json!([
            {"time": 2.5, "values": {"x": 0.0}},
            {"time": 5.0, "values": {"x": 100.0}}
        ]));
        let mut timeline = Timeline::default();
        timeline.clips.push(clip);
        timeline.snapping = false;
        timeline.trim_clip("trimmed", 3.0, 6.0, None).unwrap();
        let trimmed = timeline.clips.iter().find(|clip| clip.id == "trimmed").unwrap();
        assert!((trimmed.start - 3.0).abs() < 1e-9);
        let frames = trimmed.extra["keyframes"].as_array().unwrap();
        assert!((frames[0]["time"].as_f64().unwrap() - 3.5).abs() < 1e-9);
        assert!((frames[1]["time"].as_f64().unwrap() - 6.0).abs() < 1e-9);
    }
}
