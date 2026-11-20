use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};

const MIN_TIMELINE_SECONDS: f64 = 60.0;
const EPSILON: f64 = 1e-7;

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
    #[serde(rename = "a1")]
    A1,
    #[serde(rename = "a2")]
    A2,
}

impl TrackId {
    pub fn is_video(self) -> bool {
        matches!(self, Self::V1 | Self::V2 | Self::V3 | Self::V4)
    }

    fn lanes(self) -> &'static [TrackId] {
        if self.is_video() {
            &[Self::V1, Self::V2, Self::V3, Self::V4]
        } else {
            &[Self::A1, Self::A2]
        }
    }
}

/// A keyframe as stored on a clip. `time` is relative to the clip start,
/// which keeps animation attached to the media through moves, trims and cuts.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KeyframeSample {
    pub time: f64,
    pub x: f64,
    pub y: f64,
    pub scale_w: f64,
    pub scale_h: f64,
    pub rotation: f64,
    pub opacity: f64,
}

/// Fully sampled transform used by the shared preview/export filter builder.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TransformValues {
    pub x: f64,
    pub y: f64,
    pub scale_w: f64,
    pub scale_h: f64,
    pub rotation: f64,
    pub opacity: f64,
}

impl Default for TransformValues {
    fn default() -> Self {
        Self { x: 0.0, y: 0.0, scale_w: 100.0, scale_h: 100.0, rotation: 0.0, opacity: 100.0 }
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

fn json_path<'a>(mut value: &'a serde_json::Value, path: &[&str]) -> Option<&'a serde_json::Value> {
    for key in path {
        value = value.get(*key)?;
    }
    Some(value)
}

fn json_number(value: &serde_json::Value, path: &[&str], fallback: f64) -> f64 {
    json_path(value, path)
        .and_then(serde_json::Value::as_f64)
        .filter(|number| number.is_finite())
        .unwrap_or(fallback)
}

fn set_json_value(root: &mut serde_json::Value, path: &[&str], value: f64) {
    let Some((head, tail)) = path.split_first() else { return; };
    if !root.is_object() { *root = serde_json::json!({}); }
    let object = root.as_object_mut().unwrap();
    if tail.is_empty() {
        object.insert((*head).to_owned(), serde_json::json!(value));
        return;
    }
    let child = object.entry((*head).to_owned()).or_insert_with(|| serde_json::json!({}));
    set_json_value(child, tail, value);
}

impl Clip {
    pub fn end(&self) -> f64 {
        self.start + self.duration
    }

    /// Base (un-keyframed) transform values stored in `props.transform`.
    pub fn base_transform(&self) -> TransformValues {
        let props = self.extra.get("props").unwrap_or(&serde_json::Value::Null);
        let transform = json_path(props, &["transform"]).unwrap_or(&serde_json::Value::Null);
        TransformValues {
            x: json_number(transform, &["position", "x"], 0.0),
            y: json_number(transform, &["position", "y"], 0.0),
            scale_w: json_number(transform, &["scale", "w"], 100.0),
            scale_h: json_number(transform, &["scale", "h"], 100.0),
            rotation: json_number(transform, &["rotation"], 0.0),
            opacity: json_number(transform, &["opacity"], 100.0),
        }
    }

    /// Sorted keyframe samples with missing values filled from the base transform.
    pub fn keyframe_samples(&self) -> Vec<KeyframeSample> {
        let base = self.base_transform();
        let mut samples: Vec<_> = self.extra.get("keyframes")
            .and_then(serde_json::Value::as_array)
            .map(|keyframes| keyframes.iter().filter_map(|keyframe| {
                let time = keyframe.get("time").and_then(serde_json::Value::as_f64)
                    .filter(|time| time.is_finite())?;
                let values = keyframe.get("values").unwrap_or(&serde_json::Value::Null);
                Some(KeyframeSample {
                    time,
                    x: json_number(values, &["x"], base.x),
                    y: json_number(values, &["y"], base.y),
                    scale_w: json_number(values, &["scaleW"], base.scale_w),
                    scale_h: json_number(values, &["scaleH"], base.scale_h),
                    rotation: json_number(values, &["rotation"], base.rotation),
                    opacity: json_number(values, &["opacity"], base.opacity),
                })
            }).collect())
            .unwrap_or_default();
        samples.sort_by(|a, b| a.time.total_cmp(&b.time));
        samples
    }

    /// Linear transform at `at` seconds relative to the clip start. Values are
    /// held before the first keyframe and after the last one.
    pub fn transform_at(&self, at: f64) -> TransformValues {
        let base = self.base_transform();
        let samples = self.keyframe_samples();
        let Some(first) = samples.first().copied() else { return base; };
        let sample_to_values = |sample: &KeyframeSample| TransformValues {
            x: sample.x, y: sample.y, scale_w: sample.scale_w, scale_h: sample.scale_h,
            rotation: sample.rotation, opacity: sample.opacity,
        };
        if !at.is_finite() || at <= first.time { return sample_to_values(&first); }
        for pair in samples.windows(2) {
            let (left, right) = (pair[0], pair[1]);
            if at > right.time { continue; }
            let span = right.time - left.time;
            let t = if span.abs() < EPSILON { 1.0 } else { ((at - left.time) / span).clamp(0.0, 1.0) };
            let lerp = |a: f64, b: f64| a + (b - a) * t;
            return TransformValues {
                x: lerp(left.x, right.x),
                y: lerp(left.y, right.y),
                scale_w: lerp(left.scale_w, right.scale_w),
                scale_h: lerp(left.scale_h, right.scale_h),
                rotation: lerp(left.rotation, right.rotation),
                opacity: lerp(left.opacity, right.opacity),
            };
        }
        sample_to_values(&samples[samples.len() - 1])
    }

    pub fn shift_keyframes(&mut self, delta: f64) {
        if !delta.is_finite() || delta == 0.0 { return; }
        let Some(keyframes) = self.extra.get_mut("keyframes").and_then(serde_json::Value::as_array_mut) else { return; };
        for keyframe in keyframes.iter_mut() {
            if let Some(time) = keyframe.get("time").and_then(serde_json::Value::as_f64) {
                keyframe["time"] = serde_json::json!(time + delta);
            }
        }
        keyframes.sort_by(|a, b| {
            a["time"].as_f64().unwrap_or(0.0).total_cmp(&b["time"].as_f64().unwrap_or(0.0))
        });
    }

    fn set_base_transform(&mut self, values: TransformValues) {
        let props = self.extra.entry("props".into()).or_insert_with(|| serde_json::json!({}));
        if !props.is_object() { *props = serde_json::json!({}); }
        let transform = props.as_object_mut().unwrap().entry("transform".to_owned()).or_insert_with(|| serde_json::json!({}));
        if !transform.is_object() { *transform = serde_json::json!({}); }
        set_json_value(transform, &["position", "x"], values.x);
        set_json_value(transform, &["position", "y"], values.y);
        set_json_value(transform, &["scale", "w"], values.scale_w);
        set_json_value(transform, &["scale", "h"], values.scale_h);
        set_json_value(transform, &["rotation"], values.rotation);
        set_json_value(transform, &["opacity"], values.opacity);
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
}

impl Default for Timeline {
    fn default() -> Self {
        Self {
            clips: Vec::new(),
            duration_seconds: MIN_TIMELINE_SECONDS,
            snapping: true,
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
        let mut tracks = HashSet::from([TrackId::V1, TrackId::A1]);
        tracks.extend(self.clips.iter().map(|clip| clip.track));
        tracks
    }

    pub fn add_clip(&mut self, mut clip: Clip) -> Result<(), TimelineError> {
        clip.validate()?;
        if self.clips.iter().any(|current| current.id == clip.id) {
            return Err(TimelineError::DuplicateId(clip.id));
        }
        if clip.track.is_video() {
            clip.start = self.magnetic_start(&clip.id, clip.start.max(0.0), None);
            if self.snapping {
                clip.start = self.snap_start(&clip.id, clip.start, clip.duration, 0.0, 0.25, None);
            }
        }
        let requested_track = clip.track;
        clip.track = self.nearest_free_lane(&clip.id, requested_track, clip.start, clip.duration)
            .unwrap_or(requested_track);
        let needs_ripple = clip.track == requested_track
            && self.lane_overlaps(&clip.id, clip.track, clip.start, clip.duration);
        let id = clip.id.clone();
        self.clips.push(clip);
        if needs_ripple {
            self.ripple_lane_after(&id)?;
        }
        self.refresh_duration();
        Ok(())
    }

    /// Move a clip to the closest available lane of the same media kind.
    /// Snap candidates include other clip boundaries and the playhead.
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
        let group_id = original.extra.get("groupId").and_then(serde_json::Value::as_str).map(str::to_owned);
        let mut members: Vec<_> = self.clips.iter().filter(|clip| {
            group_id.as_deref().is_some_and(|group| clip.extra.get("groupId").and_then(serde_json::Value::as_str) == Some(group))
        }).cloned().collect();
        if members.is_empty() { members.push(original.clone()); }
        let mut start = desired_start.max(0.0);
        if original.track.is_video() {
            start = self.magnetic_start(id, start, group_id.as_deref());
        }
        if self.snapping && original.track.is_video() {
            start = self.snap_start(id, start, original.duration, playhead, snap_threshold, group_id.as_deref());
        }
        let lane = self.nearest_free_lane_excluding(id, desired_track, start, original.duration, group_id.as_deref())
            .unwrap_or(desired_track);
        let delta = start - original.start;
        let lanes = original.track.lanes();
        let old_rank = lanes.iter().position(|track| *track == original.track).unwrap_or(0) as i32;
        let new_rank = lanes.iter().position(|track| *track == lane).unwrap_or(old_rank as usize) as i32;
        let requested_track_shift = new_rank - old_rank;
        let same_kind: Vec<_> = members.iter().filter(|clip| clip.track.is_video() == original.track.is_video()).collect();
        let min_rank = same_kind.iter().map(|clip| clip.track.lanes().iter().position(|track| *track == clip.track).unwrap_or(0) as i32).min().unwrap_or(old_rank);
        let max_rank = same_kind.iter().map(|clip| clip.track.lanes().iter().position(|track| *track == clip.track).unwrap_or(0) as i32).max().unwrap_or(old_rank);
        let max_lane = lanes.len().saturating_sub(1) as i32;
        let track_shift = requested_track_shift.clamp(-min_rank, max_lane - max_rank);

        if original.track.is_video() {
            let mut vacated: Vec<_> = members.iter().filter(|clip| clip.track.is_video()).cloned().collect();
            vacated.sort_by(|a, b| a.track.lanes().iter().position(|t| *t == a.track).cmp(&b.track.lanes().iter().position(|t| *t == b.track)).then_with(|| a.start.total_cmp(&b.start)));
            for clip in &vacated {
                if clip.track != lane || (clip.start - (clip.start + delta)).abs() > EPSILON {
                    self.close_lane_gap(&clip.id, clip.track, clip.start, clip.end(), group_id.as_deref());
                }
            }
        }

        let lane_members: HashSet<_> = same_kind.iter().map(|clip| clip.id.as_str()).collect();
        for mut member in members.iter().cloned() {
            member.start = (member.start + delta).max(0.0);
            if lane_members.contains(member.id.as_str()) {
                let member_rank = member.track.lanes().iter().position(|track| *track == member.track).unwrap_or(0) as i32;
                if let Some(track) = member.track.lanes().get((member_rank + track_shift) as usize) { member.track = *track; }
            }
            self.replace(member);
        }

        let mut moved_ids: HashSet<String> = members.iter().map(|clip| clip.id.clone()).collect();
        for member in &members {
            if let Some(linked_id) = member.linked_clip_id.as_deref() {
                if moved_ids.insert(linked_id.to_owned()) {
                    if let Some(mut linked) = self.clips.iter().find(|clip| clip.id == linked_id).cloned() {
                        linked.start = (linked.start + delta).max(0.0);
                        self.replace(linked);
                    }
                }
            }
        }
        let mut ordered_clips: Vec<_> = moved_ids.into_iter()
            .filter_map(|moved_id| self.clips.iter().find(|clip| clip.id == moved_id).cloned())
            .collect();
        ordered_clips.sort_by(|a, b| {
            let rank = |track: TrackId| match track { TrackId::V1 => 0, TrackId::V2 => 1, TrackId::V3 => 2,
                TrackId::V4 => 3, TrackId::A1 => 4, TrackId::A2 => 5 };
            rank(a.track).cmp(&rank(b.track)).then_with(|| a.start.total_cmp(&b.start))
        });
        for moved in ordered_clips {
            if moved.track.is_video() { self.ripple_lane_after(&moved.id)?; }
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
        /* A left trim changes source offset. The source start and end stay
           bounded so dragging a handle back restores already-trimmed media. */
        let next_start = start.max((clip.start - source_offset).max(0.0));
        let next_offset = (source_offset + next_start - clip.start).max(0.0);
        let next_end = source_duration
            .map(|duration| end.min(next_start + (duration - next_offset).max(0.0)))
            .unwrap_or_else(|| end.min(clip.end()));
        if next_end - next_start <= EPSILON { return Err(TimelineError::InvalidRange); }

        let linked_id = clip.linked_clip_id.clone();
        let linked_track = linked_id.as_deref()
            .and_then(|linked| self.clips.iter().find(|candidate| candidate.id == linked))
            .map(|linked| linked.track);
        let mut changing_ids = vec![id];
        if let Some(linked) = linked_id.as_deref() { changing_ids.push(linked); }
        if self.clips.iter().any(|other| {
            !changing_ids.contains(&other.id.as_str())
                && (other.track == clip.track || Some(other.track) == linked_track)
                && next_start < other.end() - EPSILON
                && next_end > other.start + EPSILON
        }) {
            return Err(TimelineError::InvalidRange);
        }
        let trim_delta = next_start - clip.start;
        clip.start = next_start;
        clip.duration = next_end - next_start;
        clip.extra.insert("sourceOffset".into(), serde_json::json!(next_offset));
        /* Keyframes are clip-relative, so a left trim keeps them at the same
           source frame by moving them back by the trimmed amount. */
        clip.shift_keyframes(-trim_delta);
        self.replace(clip.clone());
        if let Some(linked_id) = linked_id {
            if let Some(mut linked) = self.clips.iter().find(|candidate| candidate.id == linked_id).cloned() {
                let linked_delta = next_start - linked.start;
                linked.start = next_start;
                linked.duration = next_end - next_start;
                linked.extra.insert("sourceOffset".into(), serde_json::json!(next_offset));
                if let Some(duration) = source_duration { linked.extra.insert("sourceDuration".into(), serde_json::json!(duration)); }
                linked.shift_keyframes(-linked_delta);
                self.replace(linked);
            }
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
        let split_offset = at - left.start;
        let original_source_offset = left.extra.get("sourceOffset")
            .and_then(serde_json::Value::as_f64).unwrap_or(0.0);
        let mut right_extra = left.extra.clone();
        right_extra.insert("sourceOffset".into(), serde_json::json!(original_source_offset + split_offset));
        let mut right = Clip {
            id: right_id,
            track: left.track,
            start: at,
            duration: left.end() - at,
            label: left.label.clone(),
            linked_clip_id: None,
            extra: right_extra,
        };
        /* The right half starts later in the same media, so its clip-relative
           keyframes move back by the split offset. */
        right.shift_keyframes(-split_offset);
        left.duration = at - left.start;
        /* The original end transition follows the tail segment after a cut. */
        if let Some(transition) = left.extra.get_mut("props").and_then(serde_json::Value::as_object_mut)
            .and_then(|props| props.get_mut("transitionOut")).and_then(serde_json::Value::as_object_mut) {
            transition.insert("type".into(), serde_json::json!("none"));
        }
        self.replace(left.clone());
        self.clips.push(right.clone());
        self.refresh_duration();
        Ok((left, right))
    }

    pub fn delete_clip(&mut self, id: &str) -> Result<Vec<Clip>, TimelineError> {
        let clip = self.find(id)?.clone();
        let mut deleted = vec![clip.clone()];
        if let Some(linked) = clip.linked_clip_id.as_deref() {
            if let Some(other) = self.clips.iter().find(|c| c.id == linked) {
                deleted.push(other.clone());
            }
        }
        let ids: HashSet<_> = deleted.iter().map(|c| c.id.as_str()).collect();
        self.clips.retain(|c| !ids.contains(c.id.as_str()));
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
        audio.track = TrackId::A1;
        audio.label = format!("{} Audio", video.label);
        audio.linked_clip_id = Some(video.id.clone());
        audio.extra.insert("kind".into(), serde_json::json!("audio"));
        audio.extra.insert("hasAudio".into(), serde_json::json!(true));
        video.linked_clip_id = Some(audio_id.clone());
        video.extra.insert("hasAudio".into(), serde_json::json!(false));
        self.replace(video);
        self.add_clip(audio)?;
        Ok(self.find(&audio_id)?.clone())
    }

    pub fn unlink_clip(&mut self, id: &str) -> Result<(), TimelineError> {
        let mut clip = self.find(id)?.clone();
        let linked_id = clip.linked_clip_id.take().ok_or(TimelineError::InvalidRange)?;
        self.replace(clip);
        if let Some(mut linked) = self.clips.iter().find(|current| current.id == linked_id).cloned() {
            linked.linked_clip_id = None;
            self.replace(linked);
        }
        Ok(())
    }

    /// Insert or update a transform keyframe. `at` is an absolute timeline
    /// time; it is stored relative to the clip so the animation follows edits.
    pub fn set_keyframe(&mut self, id: &str, at: f64, values: &serde_json::Value) -> Result<(), TimelineError> {
        if !at.is_finite() { return Err(TimelineError::InvalidRange); }
        let mut clip = self.find(id)?.clone();
        let local_at = (at - clip.start).clamp(0.0, clip.duration);
        let sampled = clip.transform_at(local_at);
        let read = |key: &str, fallback: f64| values.get(key).and_then(serde_json::Value::as_f64)
            .filter(|value| value.is_finite()).unwrap_or(fallback);
        let next = TransformValues {
            x: read("x", sampled.x),
            y: read("y", sampled.y),
            scale_w: read("scaleW", sampled.scale_w),
            scale_h: read("scaleH", sampled.scale_h),
            rotation: read("rotation", sampled.rotation),
            opacity: read("opacity", sampled.opacity),
        };
        let keyframes = clip.extra.entry("keyframes".into()).or_insert_with(|| serde_json::json!([]));
        if !keyframes.is_array() { *keyframes = serde_json::json!([]); }
        let list = keyframes.as_array_mut().unwrap();
        if let Some(existing) = list.iter_mut().find(|keyframe| {
            (keyframe["time"].as_f64().unwrap_or(f64::NEG_INFINITY) - local_at).abs() < 0.0001
        }) {
            existing["time"] = serde_json::json!(local_at);
            existing["values"] = serde_json::json!({
                "x": next.x, "y": next.y, "scaleW": next.scale_w, "scaleH": next.scale_h,
                "rotation": next.rotation, "opacity": next.opacity
            });
        } else {
            list.push(serde_json::json!({
                "time": local_at,
                "values": {
                    "x": next.x, "y": next.y, "scaleW": next.scale_w, "scaleH": next.scale_h,
                    "rotation": next.rotation, "opacity": next.opacity
                }
            }));
        }
        list.sort_by(|a, b| {
            a["time"].as_f64().unwrap_or(0.0).total_cmp(&b["time"].as_f64().unwrap_or(0.0))
        });
        clip.set_base_transform(next);
        self.replace(clip);
        Ok(())
    }

    /// Remove the keyframe closest to the requested absolute time. The base
    /// transform is kept at the sampled value so removing a key does not jump.
    pub fn remove_keyframe(&mut self, id: &str, at: f64) -> Result<(), TimelineError> {
        if !at.is_finite() { return Err(TimelineError::InvalidRange); }
        let mut clip = self.find(id)?.clone();
        let local_at = (at - clip.start).clamp(0.0, clip.duration);
        let sampled = clip.transform_at(local_at);
        if let Some(list) = clip.extra.get_mut("keyframes").and_then(serde_json::Value::as_array_mut) {
            list.retain(|keyframe| {
                (keyframe["time"].as_f64().unwrap_or(f64::NEG_INFINITY) - local_at).abs() >= 0.0001
            });
        }
        clip.set_base_transform(sampled);
        self.replace(clip);
        Ok(())
    }

    fn snap_start(
        &self,
        id: &str,
        start: f64,
        duration: f64,
        playhead: f64,
        threshold: f64,
        exclude_group: Option<&str>,
    ) -> f64 {
        let mut best: Option<(f64, f64)> = None;
        for candidate in std::iter::once(playhead).chain(
            self.clips
                .iter()
                .filter(|c| c.id != id && c.track.is_video()
                    && exclude_group.map_or(true, |group| c.extra.get("groupId").and_then(serde_json::Value::as_str) != Some(group)))
                .flat_map(|c| [c.start, c.end()]),
        ) {
            for (edge, offset) in [(start, 0.0), (start + duration, duration)] {
                let distance = (candidate - edge).abs();
                if distance <= threshold && best.map_or(true, |(d, _)| distance < d) {
                    best = Some((distance, candidate - offset));
                }
            }
        }
        best.map_or(start, |(_, value)| value.max(0.0))
    }

    /// The video storyline is always magnetic: place a clip at the latest
    /// video edge to its left. This remains active when proximity snapping is
    /// disabled in the UI.
    fn magnetic_start(&self, id: &str, desired_start: f64, exclude_group: Option<&str>) -> f64 {
        self.clips.iter()
            .filter(|clip| clip.id != id && clip.track.is_video()
                && exclude_group.map_or(true, |group| clip.extra.get("groupId").and_then(serde_json::Value::as_str) != Some(group))
                && clip.end() <= desired_start + EPSILON)
            .map(Clip::end)
            .max_by(f64::total_cmp)
            .unwrap_or(0.0)
    }

    /// Close the vacated section of a video lane when moving a clip away.
    /// Followers shift left by the removed duration, preserving their order.
    fn close_lane_gap(&mut self, moving_id: &str, track: TrackId, start: f64, end: f64, group_id: Option<&str>) {
        let duration = end - start;
        if duration <= EPSILON { return; }
        let mut followers: Vec<_> = self.clips.iter()
            .filter(|clip| clip.id != moving_id && clip.track == track && clip.start >= end - EPSILON
                && (group_id.is_none() || clip.extra.get("groupId").and_then(serde_json::Value::as_str) != group_id))
            .cloned().collect();
        followers.sort_by(|a, b| a.start.total_cmp(&b.start).then_with(|| a.id.cmp(&b.id)));
        for mut clip in followers {
            clip.start = (clip.start - duration).max(start);
            self.replace(clip);
        }
    }

    fn lane_overlaps(&self, id: &str, track: TrackId, start: f64, duration: f64) -> bool {
        self.clips.iter().any(|c| {
            c.id != id && c.track == track
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
            .filter(|c| c.id != anchor_id && c.track == anchor.track && c.end() > anchor.start + EPSILON
                && (group_id.is_none() || c.extra.get("groupId").and_then(serde_json::Value::as_str) != group_id))
            .cloned().collect();
        followers.sort_by(|a, b| a.start.total_cmp(&b.start).then_with(|| a.id.cmp(&b.id)));
        let mut cursor = anchor.end();
        for mut clip in followers {
            if clip.start < cursor - EPSILON { clip.start = cursor; }
            cursor = clip.end();
            self.replace(clip);
        }
        Ok(())
    }

    fn nearest_free_lane(
        &self,
        id: &str,
        desired: TrackId,
        start: f64,
        duration: f64,
    ) -> Result<TrackId, TimelineError> {
        self.nearest_free_lane_excluding(id, desired, start, duration, None)
    }

    fn nearest_free_lane_excluding(
        &self,
        id: &str,
        desired: TrackId,
        start: f64,
        duration: f64,
        exclude_group: Option<&str>,
    ) -> Result<TrackId, TimelineError> {
        let lanes = desired.lanes();
        let desired_index = lanes.iter().position(|lane| *lane == desired).unwrap_or(0);
        let mut candidates: Vec<_> = lanes.iter().copied().enumerate().collect();
        candidates.sort_by_key(|(index, _)| (index.abs_diff(desired_index), *index));
        candidates
            .into_iter()
            .map(|(_, lane)| lane)
            .find(|lane| {
                !self.clips.iter().any(|c| {
                    c.id != id
                        && exclude_group.map_or(true, |group| c.extra.get("groupId").and_then(serde_json::Value::as_str) != Some(group))
                        && c.track == *lane
                        && start < c.end() - EPSILON
                        && start + duration > c.start + EPSILON
                })
            })
            .ok_or(TimelineError::NoFreeTrack)
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

    fn clip_with(keyframes: serde_json::Value) -> Clip {
        let mut extra = std::collections::HashMap::new();
        extra.insert("props".to_string(), serde_json::json!({ "transform": {} }));
        extra.insert("keyframes".to_string(), keyframes);
        Clip {
            id: "clip-a".to_string(),
            track: TrackId::V1,
            start: 2.0,
            duration: 10.0,
            label: "Clip".to_string(),
            linked_clip_id: None,
            extra,
        }
    }

    #[test]
    fn transform_interpolates_piecewise_linearly() {
        let clip = clip_with(serde_json::json!([
            {"time": 0.0, "values": {"x": 0.0, "y": 0.0, "scaleW": 100.0, "scaleH": 100.0, "rotation": 0.0, "opacity": 100.0}},
            {"time": 4.0, "values": {"x": 960.0, "y": 540.0, "scaleW": 150.0, "scaleH": 150.0, "rotation": 30.0, "opacity": 100.0}},
            {"time": 8.0, "values": {"x": 0.0, "y": 0.0, "scaleW": 100.0, "scaleH": 100.0, "rotation": 0.0, "opacity": 50.0}},
        ]));
        let sampled = clip.transform_at(2.0);
        assert!((sampled.x - 480.0).abs() < 0.01, "x should be mid-segment, got {}", sampled.x);
        assert!((sampled.y - 270.0).abs() < 0.01, "y should be mid-segment, got {}", sampled.y);
        assert!((sampled.scale_w - 125.0).abs() < 0.01, "scale should be mid-segment, got {}", sampled.scale_w);
        assert!((sampled.rotation - 15.0).abs() < 0.01, "rotation should be mid-segment, got {}", sampled.rotation);
        let end = clip.transform_at(8.0);
        assert!((end.opacity - 50.0).abs() < 0.0001, "end should hold the last keyframe, got {}", end.opacity);
        let before = clip.transform_at(0.0);
        assert!((before.x - 0.0).abs() < 0.0001, "before first keyframe holds first value, got {}", before.x);
    }

    #[test]
    fn set_and_remove_keyframe_store_clip_relative_times() {
        let mut timeline = Timeline::new(vec![clip_with(serde_json::json!([]))]).unwrap();
        let clip_id = "clip-a".to_string();
        timeline
            .set_keyframe(&clip_id, 5.0, &serde_json::json!({"x": 12.0, "y": 24.0, "scaleW": 150.0, "scaleH": 150.0, "rotation": 30.0, "opacity": 80.0}))
            .unwrap();
        let clip = &timeline.clips[0];
        assert_eq!(clip.keyframe_samples().len(), 1);
        let stored_time = clip.keyframe_samples()[0].time;
        assert!((stored_time - 3.0).abs() < 0.0001, "absolute time 5.0 on a clip starting at 2.0 stores relative 3.0, got {stored_time}");
        let sampled = clip.transform_at(3.0);
        assert!((sampled.x - 12.0).abs() < 0.0001);
        assert!((sampled.opacity - 80.0).abs() < 0.0001);

        timeline.remove_keyframe(&clip_id, 5.0).unwrap();
        assert!(timeline.clips[0].keyframe_samples().is_empty());
        let base = timeline.clips[0].base_transform();
        assert!((base.x - 12.0).abs() < 0.0001);
        assert!((base.opacity - 80.0).abs() < 0.0001);
    }

    #[test]
    fn trim_and_split_keep_keyframes_stable() {
        let mut timeline = Timeline::new(vec![clip_with(serde_json::json!([
            {"time": 1.0, "values": {"x": 0.0, "y": 0.0, "scaleW": 100.0, "scaleH": 100.0, "rotation": 0.0, "opacity": 100.0}},
            {"time": 5.0, "values": {"x": 400.0, "y": 200.0, "scaleW": 120.0, "scaleH": 120.0, "rotation": 10.0, "opacity": 90.0}},
        ]))]).unwrap();
        let clip_id = "clip-a".to_string();
        let trimmed = timeline.trim_clip(&clip_id, 3.0, 6.0, None).unwrap();
        assert!((trimmed.keyframe_samples()[0].time - 0.0).abs() < 0.0001);
        assert!((trimmed.keyframe_samples()[1].time - 4.0).abs() < 0.0001);

        let (left, right) = timeline.split_clip(&clip_id, 5.0, "clip-b".to_string()).unwrap();
        assert!((left.keyframe_samples()[1].time - 4.0).abs() < 0.0001);
        assert!((right.keyframe_samples()[1].time - 2.0).abs() < 0.0001);
    }
}
