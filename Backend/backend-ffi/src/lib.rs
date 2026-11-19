//! Small C ABI surface for the native raylib UI.
//!
//! UI edits use synchronous, low-cost timeline calls. Media inspection and
//! preview jobs use submit/poll so FFmpeg never stalls the drawing thread.

use pluto_timeline_core::{Clip, Timeline, TrackId};
use serde_json::{json, Value};
mod filters;
use std::{
    collections::{hash_map::DefaultHasher, HashMap},
    ffi::{CStr, CString},
    hash::{Hash, Hasher},
    os::raw::c_char,
    panic::{catch_unwind, AssertUnwindSafe},
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc::{self, Receiver, Sender, TryRecvError},
        Arc, Mutex, OnceLock,
    },
    thread,
};

struct Backend {
    timeline: Mutex<Timeline>,
    undo: Mutex<Vec<Timeline>>,
    redo: Mutex<Vec<Timeline>>,
    next_task: AtomicU64,
    task_tx: Sender<(u64, Value)>,
    event_rx: Mutex<Receiver<String>>,
    preview_inflight: Arc<AtomicBool>,
}

static BACKEND: OnceLock<Backend> = OnceLock::new();

fn backend() -> &'static Backend {
    BACKEND.get_or_init(|| {
        let (task_tx, task_rx) = mpsc::channel::<(u64, Value)>();
        let (event_tx, event_rx) = mpsc::channel::<String>();
        let preview_inflight = Arc::new(AtomicBool::new(false));
        let worker_preview_inflight = Arc::clone(&preview_inflight);
        thread::Builder::new()
            .name("pluto-media-worker".into())
            .spawn(move || media_worker(task_rx, event_tx, worker_preview_inflight))
            .expect("failed to start Pluto media worker");
        Backend {
            timeline: Mutex::new(Timeline::default()),
            undo: Mutex::new(Vec::new()),
            redo: Mutex::new(Vec::new()),
            next_task: AtomicU64::new(1),
            task_tx,
            event_rx: Mutex::new(event_rx),
            preview_inflight,
        }
    })
}

fn media_worker(tasks: Receiver<(u64, Value)>, events: Sender<String>, preview_inflight: Arc<AtomicBool>) {
    while let Ok((id, request)) = tasks.recv() {
        let command = request["command"].as_str().unwrap_or_default().to_owned();
        let result = match command.as_str() {
            "inspect_media" => inspect_media(request["path"].as_str().unwrap_or_default()),
            "preview_frame" => render_preview(&request, id),
            "export_project" => export_project(&request, id),
            "" => Err("Background command is required".into()),
            other => Err(format!("Unsupported background command: {other}")),
        };
        if command == "preview_frame" { preview_inflight.store(false, Ordering::Release); }
        let response = match result {
            Ok(value) => {
                let mut fields = value.as_object().cloned().unwrap_or_default();
                fields.insert("requestId".into(), json!(id));
                fields.insert("command".into(), json!(command));
                fields.insert("ok".into(), json!(true));
                Value::Object(fields)
            }
            Err(error) => json!({"requestId": id, "command": command, "ok": false, "error": error}),
        };
        if events.send(response.to_string()).is_err() {
            break;
        }
    }
}

fn inspect_media(path: &str) -> Result<Value, String> {
    use std::{
        collections::hash_map::DefaultHasher,
        hash::{Hash, Hasher},
        path::Path,
    };
    let path = Path::new(path);
    if !path.is_file() {
        return Err("The selected media file could not be found".into());
    }
    let extension = path
        .extension()
        .and_then(|s| s.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    let kind = if ["png", "jpg", "jpeg", "webp", "gif"].contains(&extension.as_str()) {
        "image"
    } else if ["mp3", "wav", "m4a", "aac", "flac", "ogg"].contains(&extension.as_str()) {
        "audio"
    } else if ["mp4", "mov", "mkv", "avi", "webm", "m4v"].contains(&extension.as_str()) {
        "video"
    } else {
        return Err("This file type is not supported".into());
    };

    let mut duration = 5.0;
    let mut has_audio = false;
    if kind != "image" {
        let output = media_command(multimedia_binary("ffprobe"))
            .args([
                "-v",
                "error",
                "-show_entries",
                "format=duration:stream=codec_type,duration",
                "-of",
                "json",
            ])
            .arg(path)
            .output()
            .map_err(|e| format!("Could not start FFprobe: {e}"))?;
        if !output.status.success() {
            return Err(String::from_utf8_lossy(&output.stderr).trim().to_owned());
        }
        let data: Value = serde_json::from_slice(&output.stdout)
            .map_err(|e| format!("Invalid FFprobe output: {e}"))?;
        let seconds = |v: &Value| {
            v.as_f64()
                .or_else(|| v.as_str()?.parse().ok())
                .filter(|n: &f64| n.is_finite() && *n > 0.0)
        };
        has_audio = data["streams"]
            .as_array()
            .is_some_and(|streams| streams.iter().any(|s| s["codec_type"] == "audio"));
        duration = seconds(&data["format"]["duration"])
            .or_else(|| {
                data["streams"]
                    .as_array()?
                    .iter()
                    .filter_map(|s| seconds(&s["duration"]))
                    .reduce(f64::max)
            })
            .ok_or_else(|| "FFprobe did not report a usable duration".to_owned())?;
    }

    let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("Media");
    let mut hasher = DefaultHasher::new();
    path.to_string_lossy().hash(&mut hasher);
    let cache_dir = std::env::temp_dir().join("pluto-media-cache");
    let _ = std::fs::create_dir_all(&cache_dir);
    let thumbnail = cache_dir.join(format!("{:016x}-thumb.png", hasher.finish()));
    let waveform = cache_dir.join(format!("{:016x}-wave.png", hasher.finish()));
    let ffmpeg = multimedia_binary("ffmpeg");
    let scale_filter = "scale=384:216:force_original_aspect_ratio=decrease,pad=384:216:(ow-iw)/2:(oh-ih)/2:color=0x17181c";
    let waveform_path = if has_audio {
        let generated = if waveform.is_file() {
            true
        } else {
            media_command(&ffmpeg)
                .args(["-v", "error", "-i"])
                .arg(path)
                .args(["-filter_complex", "showwavespic=s=384x216:colors=0x9b70c8", "-frames:v", "1", "-y"])
                .arg(&waveform)
                .status()
                .is_ok_and(|status| status.success())
        };
        generated.then(|| waveform.to_string_lossy().into_owned())
    } else { None };
    let thumbnail_path = if kind == "audio" {
        waveform_path.clone()
    } else {
        let generated = if thumbnail.is_file() {
            true
        } else {
            let mut command = media_command(&ffmpeg);
            command.args(["-v", "error"]);
            if kind == "video" {
                command.args(["-ss", &format!("{:.3}", (duration * 0.35).max(0.0))]);
            }
            command.arg("-i").arg(path).args(["-frames:v", "1", "-vf", scale_filter, "-y"]).arg(&thumbnail);
            command.status().is_ok_and(|status| status.success())
        };
        generated.then(|| thumbnail.to_string_lossy().into_owned())
    };
    Ok(json!({
        "path": path.to_string_lossy(), "name": name, "duration": duration,
        "kind": kind, "hasAudio": has_audio,
        "thumbnailPath": thumbnail_path, "filmstripPath": Value::Null, "waveformPath": waveform_path
    }))
}

fn multimedia_binary(name: &str) -> std::path::PathBuf {
    use std::path::PathBuf;
    let filename = if cfg!(windows) {
        format!("{name}.exe")
    } else {
        name.to_owned()
    };
    if let Ok(dir) = std::env::var("PLUTO_FFMPEG_DIR") {
        let candidate = PathBuf::from(dir).join(&filename);
        if candidate.is_file() {
            return candidate;
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            for path in [
                dir.join(&filename),
                dir.join("ffmpeg").join(&filename),
                dir.join("resources/ffmpeg").join(&filename),
            ] {
                if path.is_file() {
                    return path;
                }
            }
        }
    }
    PathBuf::from(filename)
}

fn media_command(program: impl AsRef<std::ffi::OsStr>) -> std::process::Command {
    let mut command = std::process::Command::new(program);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    command
}

fn ffmpeg_uses_option_files(binary: &std::path::Path) -> bool {
    static SUPPORTS_OPTION_FILES: OnceLock<bool> = OnceLock::new();
    *SUPPORTS_OPTION_FILES.get_or_init(|| {
        media_command(binary).arg("-version").output().ok()
            .and_then(|output| String::from_utf8(output.stdout).ok())
            .and_then(|version| {
                let version = version.split_whitespace().nth(2)?;
                if version.starts_with("N-") || version.starts_with("git-") { Some(true) }
                else {
                    let mut parts = version.split('.');
                    let major = parts.next()?.parse::<u32>().ok()?;
                    let minor = parts.next().unwrap_or("0").parse::<u32>().ok()?;
                    Some(major > 7 || (major == 7 && minor >= 1))
                }
            })
            .unwrap_or(false)
    })
}

fn append_filter_graph_arg(command: &mut std::process::Command, binary: &std::path::Path, path: &std::path::Path) {
    if ffmpeg_uses_option_files(binary) {
        command.arg("-/filter_complex").arg(path);
    } else {
        command.arg("-filter_complex_script").arg(path);
    }
}

fn render_inputs(timeline: &Timeline) -> Result<(Vec<(String, bool)>, HashMap<String, usize>), String> {
    let mut clips: Vec<_> = timeline.clips.iter().filter(|clip| clip.extra.get("path").and_then(Value::as_str).is_some()).collect();
    clips.sort_by(|a, b| {
        let order = |track: TrackId| match track { TrackId::V1 => 0, TrackId::V2 => 1, TrackId::V3 => 2, TrackId::V4 => 3, TrackId::A1 => 4, TrackId::A2 => 5 };
        order(a.track).cmp(&order(b.track)).then_with(|| a.start.total_cmp(&b.start))
    });
    let mut inputs = Vec::with_capacity(clips.len());
    let mut indices = HashMap::with_capacity(clips.len());
    let mut path_indices = HashMap::with_capacity(clips.len());
    for clip in clips {
        let path = clip.extra.get("path").and_then(Value::as_str).unwrap_or_default();
        if !std::path::Path::new(path).is_file() { return Err(format!("Media file is missing: {path}")); }
        if let Some(&input_index) = path_indices.get(path) {
            indices.insert(clip.id.clone(), input_index);
            continue;
        }
        let image = clip.extra.get("kind").and_then(Value::as_str) == Some("image");
        let input_index = inputs.len();
        indices.insert(clip.id.clone(), input_index);
        path_indices.insert(path.to_owned(), input_index);
        inputs.push((path.to_owned(), image));
    }
    Ok((inputs, indices))
}

fn project_duration(timeline: &Timeline) -> f64 {
    timeline.clips.iter().map(Clip::end).fold(0.0, f64::max)
}

fn transition_out(clip: &Clip) -> Option<(String, f64)> {
    let transition = &clip.extra["props"]["transitionOut"];
    let kind = transition["type"].as_str()?.to_owned();
    if kind == "none" { return None; }
    let duration = transition["duration"].as_f64().filter(|v| v.is_finite()).unwrap_or(1.0).max(0.05).min(clip.duration.min(3.0).max(0.05));
    Some((kind, duration))
}

fn export_filter_graph(timeline: &Timeline, width: u32, height: u32, fps: u32) -> Result<(String, Vec<(String, bool)>, f64, bool), String> {
    let duration = project_duration(timeline);
    if duration <= 0.0 { return Err("Add at least one clip before exporting".into()); }
    let (inputs, indices) = render_inputs(timeline)?;
    let mut graph = vec![format!("color=c=black:s={width}x{height}:r={fps}:d={duration:.5},format=rgba[base0]")];
    let mut video_clips: Vec<_> = timeline.clips.iter().filter(|clip| clip.track.is_video() && clip.extra.get("kind").and_then(Value::as_str) != Some("audio") && indices.contains_key(&clip.id)).collect();
    video_clips.sort_by(|a, b| {
        let order = |track: TrackId| match track { TrackId::V1 => 0, TrackId::V2 => 1, TrackId::V3 => 2, TrackId::V4 => 3, TrackId::A1 => 4, TrackId::A2 => 5 };
        order(a.track).cmp(&order(b.track)).then_with(|| a.start.total_cmp(&b.start))
    });
    let mut base = "base0".to_owned();
    for (n, clip) in video_clips.iter().enumerate() {
        let value = serde_json::to_value(*clip).map_err(|e| e.to_string())?;
        let adjacent = |a: &Clip, b: &Clip| a.track == b.track && (a.end() - b.start).abs() <= 0.04;
        let incoming = if n > 0 && adjacent(video_clips[n - 1], clip) { transition_out(video_clips[n - 1]) } else { None };
        let has_next = n + 1 < video_clips.len() && adjacent(clip, video_clips[n + 1]);
        let outgoing = if has_next { transition_out(clip) } else { None };
        let (render_start, render_length) = incoming.as_ref().map_or((clip.start, clip.duration), |(_, d)| (clip.start - d, clip.duration + d));
        let transition_in = incoming.as_ref().map(|(kind, d)| (kind.as_str(), *d));
        let transition_out = outgoing.as_ref().map(|(kind, d)| (kind.as_str(), *d));
        let (filters, x, y) = filters::video_filters(&value, width, height, render_start, render_length, transition_in, transition_out);
        let input = indices[&clip.id];
        let layer = format!("v{n}");
        let next = format!("base{}", n + 1);
        graph.push(format!("[{input}:v]{filters}[{layer}]"));
        graph.push(format!("[{base}][{layer}]overlay=x={x}:y={y}:eof_action=pass:format=auto[{next}]"));
        base = next;
    }
    graph.push(format!("[{base}]format=yuv420p[vout]"));

    let mut audio_labels = Vec::new();
    for clip in &timeline.clips {
        let is_audio = !clip.track.is_video() || clip.extra.get("hasAudio").and_then(Value::as_bool).unwrap_or(false);
        let Some(&input) = indices.get(&clip.id) else { continue; };
        if !is_audio { continue; }
        let kind = clip.extra.get("kind").and_then(Value::as_str).unwrap_or_default();
        if kind == "image" { continue; }
        let value = serde_json::to_value(clip).map_err(|e| e.to_string())?;
        let af = filters::audio_filters(&value, clip.duration, duration);
        let label = format!("a{}", audio_labels.len());
        graph.push(format!("[{input}:a]{af}[{label}]"));
        audio_labels.push(label);
    }
    if audio_labels.len() == 1 {
        graph.push(format!("[{}]atrim=duration={duration:.5},asetpts=PTS-STARTPTS[aout]", audio_labels[0]));
    } else if !audio_labels.is_empty() {
        let inputs = audio_labels.iter().map(|label| format!("[{label}]")).collect::<String>();
        graph.push(format!("{inputs}amix=inputs={}:duration=longest:normalize=0,atrim=duration={duration:.5},asetpts=PTS-STARTPTS[aout]", audio_labels.len()));
    }
    Ok((graph.join(";"), inputs, duration, !audio_labels.is_empty()))
}

fn filter_script(id: u64, contents: &str) -> Result<std::path::PathBuf, String> {
    let dir = std::env::temp_dir().join("pluto-media-cache");
    std::fs::create_dir_all(&dir).map_err(|e| format!("Could not create render cache: {e}"))?;
    let path = dir.join(format!("graph-{id}.txt"));
    std::fs::write(&path, contents).map_err(|e| format!("Could not write FFmpeg graph: {e}"))?;
    Ok(path)
}

fn ffmpeg_error(output: std::process::Output) -> String {
    let detail = String::from_utf8_lossy(&output.stderr);
    let detail = detail.trim();
    if detail.is_empty() { "FFmpeg could not render this request".into() }
    else { detail.chars().take(320).collect() }
}

fn export_project(request: &Value, id: u64) -> Result<Value, String> {
    let timeline: Timeline = serde_json::from_value(request["timeline"].clone()).map_err(|e| format!("Invalid timeline: {e}"))?;
    let output_path = request["path"].as_str().ok_or("Export path is required")?;
    let width = request["width"].as_u64().unwrap_or(1920).clamp(320, 3840) as u32;
    let height = request["height"].as_u64().unwrap_or(1080).clamp(180, 2160) as u32;
    let fps = request["fps"].as_u64().unwrap_or(30).clamp(1, 120) as u32;
    let (graph, inputs, duration, has_audio) = export_filter_graph(&timeline, width, height, fps)?;
    let script = filter_script(id, &graph)?;
    let ffmpeg = multimedia_binary("ffmpeg");
    let mut command = media_command(&ffmpeg);
    command.args(["-hide_banner", "-loglevel", "error"]);
    append_filter_graph_arg(&mut command, &ffmpeg, &script);
    for (path, image) in inputs {
        if image { command.args(["-loop", "1", "-framerate", &fps.to_string()]); }
        command.arg("-i").arg(path);
    }
    command.args(["-map", "[vout]"]);
    if has_audio { command.args(["-map", "[aout]"]); }
    command.args(["-t", &format!("{duration:.5}"), "-r", &fps.to_string(), "-c:v", "libx264", "-preset", "veryfast", "-crf", "18", "-pix_fmt", "yuv420p"]);
    if has_audio { command.args(["-c:a", "aac", "-b:a", "192k"]); } else { command.arg("-an"); }
    command.args(["-movflags", "+faststart", "-y"]).arg(output_path);
    let result = command.output().map_err(|e| format!("Could not start FFmpeg: {e}"))?;
    let _ = std::fs::remove_file(script);
    if !result.status.success() { return Err(ffmpeg_error(result)); }
    Ok(json!({"outputPath": output_path, "duration": duration, "width": width, "height": height, "fps": fps}))
}

fn preview_cache_path(timeline: &Timeline, at: f64, height: u32) -> Result<std::path::PathBuf, String> {
    let mut hasher = DefaultHasher::new();
    "stella-preview-v1".hash(&mut hasher);
    serde_json::to_vec(timeline).map_err(|e| e.to_string())?.hash(&mut hasher);
    at.to_bits().hash(&mut hasher);
    height.hash(&mut hasher);
    for clip in &timeline.clips {
        if let Some(path) = clip.extra.get("path").and_then(Value::as_str) {
            path.hash(&mut hasher);
            if let Ok(metadata) = std::fs::metadata(path) {
                metadata.len().hash(&mut hasher);
                if let Ok(modified) = metadata.modified() {
                    if let Ok(age) = modified.duration_since(std::time::UNIX_EPOCH) { age.as_nanos().hash(&mut hasher); }
                }
            }
        }
    }
    let cache_dir = std::env::temp_dir().join("pluto-media-cache");
    Ok(cache_dir.join(format!("preview-v1-{:016x}.png", hasher.finish())))
}

fn prune_preview_cache(directory: &std::path::Path, keep: &std::path::Path) {
    static PRUNE_TICK: AtomicU64 = AtomicU64::new(0);
    if PRUNE_TICK.fetch_add(1, Ordering::Relaxed) % 32 != 0 { return; }
    let mut entries: Vec<_> = match std::fs::read_dir(directory) {
        Ok(items) => items.filter_map(Result::ok).filter_map(|entry| {
            let path = entry.path();
            let name = path.file_name()?.to_str()?;
            if !name.starts_with("preview-v1-") || path.extension().and_then(|s| s.to_str()) != Some("png") { return None; }
            let modified = entry.metadata().and_then(|meta| meta.modified()).unwrap_or(std::time::UNIX_EPOCH);
            Some((modified, path))
        }).collect(),
        Err(_) => return,
    };
    if entries.len() <= 128 { return; }
    entries.sort_by_key(|(modified, _)| *modified);
    let remove_count = entries.len() - 128;
    for (_, path) in entries.into_iter().filter(|(_, path)| path.as_path() != keep).take(remove_count) {
        let _ = std::fs::remove_file(path);
    }
}

fn render_preview(request: &Value, id: u64) -> Result<Value, String> {
    let timeline: Timeline = serde_json::from_value(request["timeline"].clone()).map_err(|e| format!("Invalid timeline: {e}"))?;
    let at = request["at"].as_f64().filter(|value| value.is_finite() && *value >= 0.0).unwrap_or(0.0);
    let height = request["height"].as_u64().unwrap_or(720).clamp(240, 1080) as u32;
    let width = ((height as f64 * 16.0 / 9.0).round() as u32) & !1;
    let adjacent_transition = |clip: &Clip| timeline.clips.iter().find(|previous| previous.track == clip.track && (previous.end() - clip.start).abs() <= 0.04).and_then(transition_out);
    let mut active: Vec<_> = timeline.clips.iter().filter(|clip| {
        let transition_start = adjacent_transition(clip).map(|(_, d)| clip.start - d);
        clip.track.is_video() && (clip.start <= at && at < clip.end() || transition_start.is_some_and(|start| start <= at && at < clip.end()))
        && clip.extra.get("kind").and_then(Value::as_str) != Some("audio")
        && clip.extra.get("path").and_then(Value::as_str).is_some()
    }).collect();
    active.sort_by(|a, b| {
        let order = |track: TrackId| match track { TrackId::V1 => 0, TrackId::V2 => 1, TrackId::V3 => 2, TrackId::V4 => 3, TrackId::A1 => 4, TrackId::A2 => 5 };
        order(a.track).cmp(&order(b.track)).then_with(|| a.start.total_cmp(&b.start))
    });
    for clip in &active {
        let path = clip.extra.get("path").and_then(Value::as_str).unwrap_or_default();
        if !std::path::Path::new(path).is_file() { return Err(format!("Media file is missing: {path}")); }
    }
    let out = preview_cache_path(&timeline, at, height)?;
    std::fs::create_dir_all(out.parent().ok_or("Invalid preview cache path")?).map_err(|e| e.to_string())?;
    if out.metadata().is_ok_and(|meta| meta.len() > 24) {
        return Ok(json!({"previewPath": out.to_string_lossy(), "at": at, "width": width, "height": height}));
    }
    let _ = std::fs::remove_file(&out);
    let render_out = out.with_file_name(format!("preview-render-{id}.png"));
    let mut graph = vec![format!("color=c=black:s={width}x{height}:r=30:d=1,format=rgba[base0]")];
    let mut inputs: Vec<(String, bool, f64)> = Vec::new();
    let mut base = "base0".to_owned();
    for (n, clip) in active.iter().enumerate() {
        let path = clip.extra.get("path").and_then(Value::as_str).unwrap_or_default().to_owned();
        if !std::path::Path::new(&path).is_file() { return Err(format!("Media file is missing: {path}")); }
        let image = clip.extra.get("kind").and_then(Value::as_str) == Some("image");
        let source_offset = clip.extra.get("sourceOffset").and_then(Value::as_f64).unwrap_or(0.0);
        let incoming = adjacent_transition(clip);
        let render_start = incoming.as_ref().map_or(clip.start, |(_, d)| clip.start - d);
        let source_at = source_offset + (at - render_start).max(0.0);
        let input = inputs.len();
        inputs.push((path, image, source_at));
        let mut value = serde_json::to_value(*clip).map_err(|e| e.to_string())?;
        value["sourceOffset"] = json!(0.0);
        let (mut vf, x, y) = filters::video_filters(&value, width, height, 0.0, 1.0 / 30.0, None, None);
        if let Some((kind, duration)) = incoming.as_ref().filter(|_| at < clip.start) {
            let progress = ((at - render_start) / duration).clamp(0.0, 1.0);
            if kind.as_str() == "wipe" {
                vf.push_str(&format!(",geq=a='alpha(X,Y)*if(lte(X/W,{progress:.5}),1,0)'"));
            } else if kind.as_str() == "iris" {
                vf.push_str(&format!(",geq=a='alpha(X,Y)*if(lte(hypot(X-W/2,Y-H/2),min(W,H)*0.5*{progress:.5}),1,0)'"));
            } else if kind.as_str() == "dip" {
                if progress < 0.5 { vf.push_str(",colorchannelmixer=aa=0"); }
                else {
                    let brightness = ((progress - 0.5) * 2.0).clamp(0.0, 1.0);
                    vf.push_str(&format!(",lutrgb=r='val*{brightness:.5}':g='val*{brightness:.5}':b='val*{brightness:.5}'"));
                }
            } else {
                vf.push_str(&format!(",colorchannelmixer=aa={progress:.5}"));
            }
        } else if let Some((_kind, duration)) = transition_out(clip).filter(|(kind, _)| kind.as_str() == "dip") {
            if at >= clip.end() - duration {
                let progress = ((at - (clip.end() - duration)) / duration).clamp(0.0, 1.0);
                let brightness = (1.0 - progress * 2.0).clamp(0.0, 1.0);
                vf.push_str(&format!(",lutrgb=r='val*{brightness:.5}':g='val*{brightness:.5}':b='val*{brightness:.5}'"));
            }
        }
        let label = format!("pv{n}");
        let next = format!("base{}", n + 1);
        graph.push(format!("[{input}:v]{vf}[{label}]"));
        graph.push(format!("[{base}][{label}]overlay=x={x}:y={y}:eof_action=pass:format=auto[{next}]"));
        base = next;
    }
    graph.push(format!("[{base}]format=rgba[preview]"));
    let script = filter_script(id, &graph.join(";"))?;
    let ffmpeg = multimedia_binary("ffmpeg");
    let mut command = media_command(&ffmpeg);
    command.args(["-hide_banner", "-loglevel", "error", "-filter_threads", "1"]);
    append_filter_graph_arg(&mut command, &ffmpeg, &script);
    for (path, image, source_at) in inputs {
        if !image { command.args(["-ss", &format!("{source_at:.4}")]); }
        else { command.args(["-loop", "1", "-framerate", "30"]); }
        command.arg("-i").arg(path);
    }
    command.args(["-map", "[preview]", "-frames:v", "1", "-threads", "1", "-y"]).arg(&render_out);
    let result = command.output().map_err(|e| format!("Could not start FFmpeg: {e}"))?;
    let _ = std::fs::remove_file(script);
    if !result.status.success() {
        let _ = std::fs::remove_file(&render_out);
        return Err(ffmpeg_error(result));
    }
    std::fs::rename(&render_out, &out).map_err(|e| format!("Could not cache preview frame: {e}"))?;
    prune_preview_cache(out.parent().ok_or("Invalid preview cache path")?, &out);
    Ok(json!({"previewPath": out.to_string_lossy(), "at": at, "width": width, "height": height}))
}

fn dispatch(request: Value) -> Result<Value, String> {
    let backend = backend();
    let mut timeline = backend
        .timeline
        .lock()
        .map_err(|_| "Timeline lock failed".to_owned())?;
    let command = request["command"].as_str().ok_or("Command is required")?;
    match command {
        "timeline_get" => serde_json::to_value(timeline.clone()).map_err(|e| e.to_string()),
        "timeline_undo" => {
            let previous = backend.undo.lock().map_err(|_| "Undo history lock failed")?.pop()
                .ok_or("Nothing to undo")?;
            backend.redo.lock().map_err(|_| "Redo history lock failed")?.push(timeline.clone());
            *timeline = previous;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_redo" => {
            let next = backend.redo.lock().map_err(|_| "Redo history lock failed")?.pop()
                .ok_or("Nothing to redo")?;
            backend.undo.lock().map_err(|_| "Undo history lock failed")?.push(timeline.clone());
            *timeline = next;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_initialize" => {
            let before = timeline.clone();
            let clips: Vec<Clip> =
                serde_json::from_value(request["clips"].clone()).map_err(|e| e.to_string())?;
            let snapping = timeline.snapping;
            *timeline = Timeline::new(clips).map_err(|e| format!("{e:?}"))?;
            timeline.snapping = snapping;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_add_clip" => {
            let before = timeline.clone();
            let clip: Clip =
                serde_json::from_value(request["clip"].clone()).map_err(|e| e.to_string())?;
            timeline.add_clip(clip).map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_group_clips" => {
            let before = timeline.clone();
            let ids: Vec<String> = serde_json::from_value(request["ids"].clone()).map_err(|e| e.to_string())?;
            let group_id = request["groupId"].as_str().ok_or("Group id is required")?;
            timeline.group_clips(&ids, group_id).map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_ungroup_clip" => {
            let before = timeline.clone();
            timeline.ungroup_clip(request["id"].as_str().ok_or("Clip id is required")?)
                .map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_separate_audio" => {
            let before = timeline.clone();
            let id = request["id"].as_str().ok_or("Clip id is required")?;
            let audio_id = request["audioId"].as_str().ok_or("Audio clip id is required")?;
            timeline.separate_audio(id, audio_id.to_owned()).map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_unlink_clip" => {
            let before = timeline.clone();
            timeline.unlink_clip(request["id"].as_str().ok_or("Clip id is required")?)
                .map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_move_clip" => {
            let before = timeline.clone();
            let track: TrackId =
                serde_json::from_value(request["track"].clone()).map_err(|e| e.to_string())?;
            timeline
                .move_clip(
                    request["id"].as_str().ok_or("Clip id is required")?,
                    request["start"].as_f64().ok_or("Start is required")?,
                    track,
                    request["playhead"].as_f64().unwrap_or(0.0),
                    request["snapThreshold"].as_f64().unwrap_or(0.0),
                )
                .map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_trim_clip" => {
            let before = timeline.clone();
            timeline
                .trim_clip(
                    request["id"].as_str().ok_or("Clip id is required")?,
                    request["start"].as_f64().ok_or("Start is required")?,
                    request["end"].as_f64().ok_or("End is required")?,
                )
                .map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_split_clip" => {
            let before = timeline.clone();
            timeline
                .split_clip(
                    request["id"].as_str().ok_or("Clip id is required")?,
                    request["at"].as_f64().ok_or("Split time is required")?,
                    request["rightId"]
                        .as_str()
                        .ok_or("New clip id is required")?
                        .to_owned(),
                )
                .map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_delete_clip" => {
            let before = timeline.clone();
            timeline
                .delete_clip(request["id"].as_str().ok_or("Clip id is required")?)
                .map_err(|e| format!("{e:?}"))?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_set_snapping" => {
            let before = timeline.clone();
            timeline.snapping = request["enabled"]
                .as_bool()
                .ok_or("Snapping value is required")?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_add_keyframe" => {
            let before = timeline.clone();
            let id = request["id"].as_str().ok_or("Clip id is required")?;
            let at = request["at"].as_f64().filter(|value| value.is_finite() && *value >= 0.0).ok_or("Keyframe time is invalid")?;
            let clip = timeline.clips.iter_mut().find(|clip| clip.id == id)
                .ok_or_else(|| format!("Clip {id} was not found"))?;
            let keyframes = clip.extra.entry("keyframes".into()).or_insert_with(|| json!([]));
            let list = keyframes.as_array_mut().ok_or("Clip keyframes are invalid")?;
            if !list.iter().any(|keyframe| (keyframe["time"].as_f64().unwrap_or(-1.0) - at).abs() < 0.0001) {
                list.push(json!({"time": at, "values": request["values"].clone()}));
                list.sort_by(|a, b| a["time"].as_f64().unwrap_or(0.0).total_cmp(&b["time"].as_f64().unwrap_or(0.0)));
                save_history(backend, before)?;
            }
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_set_clip_property" => {
            let before = timeline.clone();
            let id = request["id"].as_str().ok_or("Clip id is required")?;
            let path = request["path"].as_str().ok_or("Property path is required")?;
            let clip = timeline.clips.iter_mut().find(|clip| clip.id == id)
                .ok_or_else(|| format!("Clip {id} was not found"))?;
            set_clip_property(clip, path, request["value"].clone())?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_set_clip_properties" => {
            let before = timeline.clone();
            let id = request["id"].as_str().ok_or("Clip id is required")?;
            let updates = request["properties"].as_array().ok_or("Property updates are required")?;
            let clip = timeline.clips.iter_mut().find(|clip| clip.id == id)
                .ok_or_else(|| format!("Clip {id} was not found"))?;
            let mut updated = clip.clone();
            for update in updates {
                let path = update["path"].as_str().ok_or("Property path is required")?;
                set_clip_property(&mut updated, path, update["value"].clone())?;
            }
            *clip = updated;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        _ => Err(format!("Unsupported command: {command}")),
    }
}

fn save_history(backend: &Backend, before: Timeline) -> Result<(), String> {
    let mut undo = backend.undo.lock().map_err(|_| "Undo history lock failed")?;
    undo.push(before);
    if undo.len() > 64 { undo.remove(0); }
    backend.redo.lock().map_err(|_| "Redo history lock failed")?.clear();
    Ok(())
}

fn set_json_path(root: &mut Value, parts: &[&str], value: Value) {
    let Some((head, tail)) = parts.split_first() else { return; };
    if tail.is_empty() {
        root[head] = value;
        return;
    }
    if !root[head].is_object() { root[head] = json!({}); }
    set_json_path(&mut root[head], tail, value);
}

fn set_clip_property(clip: &mut Clip, path: &str, value: Value) -> Result<(), String> {
    let parts: Vec<_> = path.split('.').filter(|part| !part.is_empty()).collect();
    if parts.is_empty() || parts.len() > 8 { return Err("Invalid property path".into()); }
    let mut props = clip.extra.remove("props").unwrap_or_else(|| json!({}));
    if !props.is_object() { props = json!({}); }
    set_json_path(&mut props, &parts, value);
    clip.extra.insert("props".into(), props);
    Ok(())
}

fn c_string(value: String) -> *mut c_char {
    CString::new(value.replace('\0', "\\0"))
        .expect("NUL bytes replaced")
        .into_raw()
}

/// Synchronously dispatches inexpensive timeline commands. Returns owned JSON;
/// release it with `pluto_backend_free`.
#[no_mangle]
pub unsafe extern "C" fn pluto_backend_request(input: *const c_char) -> *mut c_char {
    let output = catch_unwind(AssertUnwindSafe(|| {
        if input.is_null() {
            return json!({"ok":false,"error":"Null request"}).to_string();
        }
        let text = CStr::from_ptr(input).to_string_lossy();
        match serde_json::from_str::<Value>(&text)
            .map_err(|e| e.to_string())
            .and_then(dispatch)
        {
            Ok(value) => json!({"ok":true,"result":value}).to_string(),
            Err(error) => json!({"ok":false,"error":error}).to_string(),
        }
    }))
    .unwrap_or_else(|_| "{\"ok\":false,\"error\":\"Backend panic\"}".into());
    c_string(output)
}

/// Queues an expensive task and returns its request id. Results arrive via poll.
#[no_mangle]
pub unsafe extern "C" fn pluto_backend_submit(input: *const c_char) -> u64 {
    catch_unwind(AssertUnwindSafe(|| {
        if input.is_null() {
            return 0;
        }
        let mut request: Value = match serde_json::from_str(&CStr::from_ptr(input).to_string_lossy()) {
            Ok(value) => value,
            Err(_) => return 0,
        };
        let backend = backend();
        let command = request["command"].as_str().unwrap_or_default().to_owned();
        let is_preview = command == "preview_frame";
        if is_preview {
            if backend.preview_inflight.swap(true, Ordering::AcqRel) { return 0; }
        }
        if matches!(command.as_str(), "preview_frame" | "export_project") && request["timeline"].is_null() {
            let timeline = match backend.timeline.lock() {
                Ok(timeline) => timeline.clone(),
                Err(_) => { if is_preview { backend.preview_inflight.store(false, Ordering::Release); } return 0; }
            };
            request["timeline"] = match serde_json::to_value(timeline) {
                Ok(value) => value,
                Err(_) => { if is_preview { backend.preview_inflight.store(false, Ordering::Release); } return 0; }
            };
        }
        let id = backend.next_task.fetch_add(1, Ordering::Relaxed);
        if backend.task_tx.send((id, request)).is_ok() {
            id
        } else {
            if is_preview { backend.preview_inflight.store(false, Ordering::Release); }
            0
        }
    }))
    .unwrap_or(0)
}

/// Returns one completed background task as JSON, or null when the queue is empty.
#[no_mangle]
pub extern "C" fn pluto_backend_poll() -> *mut c_char {
    catch_unwind(AssertUnwindSafe(|| match backend().event_rx.lock() {
        Ok(rx) => match rx.try_recv() {
            Ok(value) => c_string(value),
            Err(TryRecvError::Empty | TryRecvError::Disconnected) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }))
    .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn pluto_backend_free(value: *mut c_char) {
    if !value.is_null() {
        drop(CString::from_raw(value));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_dispatch_returns_structured_error_for_unknown_command() {
        let error = dispatch(json!({"command":"unknown"})).unwrap_err();
        assert!(error.contains("Unsupported command"));
    }
}
