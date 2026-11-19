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

    pub fn add_clip(&mut self, clip: Clip) -> Result<(), TimelineError> {
        clip.validate()?;
        if self.clips.iter().any(|current| current.id == clip.id) {
            return Err(TimelineError::DuplicateId(clip.id));
        }
        self.clips.push(clip);
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
        let mut start = desired_start.max(0.0);
        if self.snapping {
            start = self.snap_start(id, start, original.duration, playhead, snap_threshold);
        }
        let lane = self.nearest_free_lane(id, desired_track, start, original.duration)?;
        let delta = start - original.start;
        let mut moved = original.clone();
        moved.start = start;
        moved.track = lane;
        self.replace(moved.clone());

        if let Some(linked_id) = original.linked_clip_id.as_deref() {
            if let Some(mut linked) = self.clips.iter().find(|c| c.id == linked_id).cloned() {
                linked.start = (linked.start + delta).max(0.0);
                self.replace(linked);
            }
        }
        self.refresh_duration();
        Ok(moved)
    }

    pub fn trim_clip(&mut self, id: &str, start: f64, end: f64) -> Result<Clip, TimelineError> {
        if !start.is_finite() || !end.is_finite() || start < 0.0 || end - start <= EPSILON {
            return Err(TimelineError::InvalidRange);
        }
        let mut clip = self.find(id)?.clone();
        if self.clips.iter().any(|other| {
            other.id != id
                && other.track == clip.track
                && start < other.end() - EPSILON
                && end > other.start + EPSILON
        }) {
            return Err(TimelineError::InvalidRange);
        }
        clip.start = start;
        clip.duration = end - start;
        self.replace(clip.clone());
        self.refresh_duration();
        Ok(clip)
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
        let right = Clip {
            id: right_id,
            track: left.track,
            start: at,
            duration: left.end() - at,
            label: left.label.clone(),
            linked_clip_id: None,
            extra: left.extra.clone(),
        };
        left.duration = at - left.start;
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

    fn snap_start(
        &self,
        id: &str,
        start: f64,
        duration: f64,
        playhead: f64,
        threshold: f64,
    ) -> f64 {
        let mut best: Option<(f64, f64)> = None;
        for candidate in std::iter::once(playhead).chain(
            self.clips
                .iter()
                .filter(|c| c.id != id)
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

    fn nearest_free_lane(
        &self,
        id: &str,
        desired: TrackId,
        start: f64,
        duration: f64,
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
