//! Small C ABI surface for the native raylib UI.
//!
//! UI edits use synchronous, low-cost timeline calls. Media inspection and
//! preview jobs use submit/poll so FFmpeg never stalls the drawing thread.

use pluto_timeline_core::{Clip, Timeline, TrackId};
use serde_json::{json, Value};
mod filters;
mod gpu_preview;
use std::{
    collections::{hash_map::DefaultHasher, HashMap},
    ffi::{CStr, CString},
    hash::{Hash, Hasher},
    io::Read,
    os::raw::c_char,
    panic::{catch_unwind, AssertUnwindSafe},
    process::{Child, Stdio},
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc::{self, Receiver, Sender, SyncSender, TryRecvError},
        Arc, Mutex, OnceLock,
    },
    thread::{self, JoinHandle},
    time::{Instant, SystemTime, UNIX_EPOCH},
};

struct Backend {
    timeline: Mutex<Timeline>,
    undo: Mutex<Vec<Timeline>>,
    redo: Mutex<Vec<Timeline>>,
    next_task: AtomicU64,
    task_tx: Sender<(u64, Value)>,
    preview_task_tx: Sender<(u64, Value)>,
    proxy_task_tx: Sender<(u64, Value)>,
    event_rx: Mutex<Receiver<String>>,
    preview_inflight: Arc<AtomicBool>,
}

static BACKEND: OnceLock<Backend> = OnceLock::new();
static MEDIA_PROXIES: OnceLock<Mutex<HashMap<String, std::path::PathBuf>>> = OnceLock::new();
static PREVIEW_FRAME: OnceLock<Mutex<PreviewFrame>> = OnceLock::new();

struct PreviewFrame {
    request_id: u64,
    width: u32,
    height: u32,
    at: f64,
    pixels: Vec<u8>,
}

impl Default for PreviewFrame {
    fn default() -> Self { Self { request_id: 0, width: 0, height: 0, at: 0.0, pixels: Vec::new() } }
}

const PREVIEW_DECODE_QUEUE_DEPTH: usize = 6;

struct PreviewStream {
    signature: u64,
    width: u32,
    height: u32,
    fps: u32,
    origin_index: u64,
    child: Arc<Mutex<Child>>,
    script: std::path::PathBuf,
    next_index: u64,
    last_index: Option<u64>,
    pixels: Vec<u8>,
    frames: Option<Receiver<Result<(u64, Vec<u8>), String>>>,
    recycle: SyncSender<Vec<u8>>,
    stop: Sender<()>,
    decoder_thread: Option<JoinHandle<()>>,
}

impl Drop for PreviewStream {
    fn drop(&mut self) {
        // Disconnect the bounded frame queue before joining. This unblocks a
        // decoder that is currently waiting to publish into a full queue.
        let _ = self.stop.send(());
        drop(self.frames.take());
        if let Ok(mut child) = self.child.lock() {
            let _ = child.kill();
            let _ = child.wait();
        }
        if let Some(worker) = self.decoder_thread.take() {
            let _ = worker.join();
        }
        let _ = std::fs::remove_file(&self.script);
    }
}

#[derive(Default)]
struct PreviewEngine { stream: Option<PreviewStream> }

fn publish_preview_buffer(request_id: u64, width: u32, height: u32, at: f64, pixels: &mut Vec<u8>, reuse_last: bool) -> Result<(), String> {
    let slot = PREVIEW_FRAME.get_or_init(|| Mutex::new(PreviewFrame::default()));
    let mut slot = slot.lock().map_err(|_| "Preview frame lock failed")?;
    // Never allow a "reuse" hint to leave the shared slot uninitialized or
    // with a mismatched resolution. This also makes first-frame publication
    // robust if a caller's cache state is reset independently.
    if !reuse_last || slot.pixels.len() != pixels.len() {
        slot.pixels.resize(pixels.len(), 0);
        std::mem::swap(&mut slot.pixels, pixels);
    }
    slot.request_id = request_id;
    slot.width = width;
    slot.height = height;
    slot.at = at;
    Ok(())
}

fn publish_black_frame(request_id: u64, width: u32, height: u32, at: f64) -> Result<(), String> {
    let slot = PREVIEW_FRAME.get_or_init(|| Mutex::new(PreviewFrame::default()));
    let mut slot = slot.lock().map_err(|_| "Preview frame lock failed")?;
    slot.pixels.resize(width as usize * height as usize * 4, 0);
    for pixel in slot.pixels.chunks_exact_mut(4) { pixel[3] = 255; }
    slot.request_id = request_id;
    slot.width = width;
    slot.height = height;
    slot.at = at;
    Ok(())
}

fn remember_media_proxy(source: &str, proxy: std::path::PathBuf) {
    let cache = MEDIA_PROXIES.get_or_init(|| Mutex::new(HashMap::new()));
    if let Ok(mut cache) = cache.lock() { cache.insert(source.to_owned(), proxy); }
}

fn preview_source_path(clip: &Clip) -> String {
    let source = clip.extra.get("path").and_then(Value::as_str).unwrap_or_default();
    let Some(cache) = MEDIA_PROXIES.get() else { return source.to_owned(); };
    let Ok(cache) = cache.lock() else { return source.to_owned(); };
    cache.get(source).filter(|proxy| proxy.is_file())
        .map(|proxy| proxy.to_string_lossy().into_owned())
        .unwrap_or_else(|| source.to_owned())
}

fn backend() -> &'static Backend {
    BACKEND.get_or_init(|| {
        let (task_tx, task_rx) = mpsc::channel::<(u64, Value)>();
        let (preview_task_tx, preview_task_rx) = mpsc::channel::<(u64, Value)>();
        let (proxy_task_tx, proxy_task_rx) = mpsc::channel::<(u64, Value)>();
        let (event_tx, event_rx) = mpsc::channel::<String>();
        let preview_inflight = Arc::new(AtomicBool::new(false));
        let preview_events = event_tx.clone();
        let preview_worker_inflight = Arc::clone(&preview_inflight);
        let proxy_events = event_tx.clone();
        let proxy_tasks = proxy_task_tx.clone();
        thread::Builder::new()
            .name("pluto-media-worker".into())
            .spawn(move || media_worker(task_rx, event_tx, proxy_tasks))
            .expect("failed to start Pluto media worker");
        thread::Builder::new()
            .name("stella-preview-worker".into())
            .spawn(move || preview_worker(preview_task_rx, preview_events, preview_worker_inflight))
            .expect("failed to start Stella preview worker");
        thread::Builder::new()
            .name("stella-proxy-worker".into())
            .spawn(move || proxy_worker(proxy_task_rx, proxy_events))
            .expect("failed to start Stella proxy worker");
        Backend {
            timeline: Mutex::new(Timeline::default()),
            undo: Mutex::new(Vec::new()),
            redo: Mutex::new(Vec::new()),
            next_task: AtomicU64::new(1),
            task_tx,
            preview_task_tx,
            proxy_task_tx,
            event_rx: Mutex::new(event_rx),
            preview_inflight,
        }
    })
}

fn media_worker(tasks: Receiver<(u64, Value)>, events: Sender<String>, proxy_tasks: Sender<(u64, Value)>) {
    while let Ok((id, request)) = tasks.recv() {
        let command = request["command"].as_str().unwrap_or_default().to_owned();
        let result = match command.as_str() {
            "inspect_media" => {
                let source = request["path"].as_str().unwrap_or_default();
                let result = inspect_media(source);
                if let Ok(metadata) = &result {
                    if let Some(proxy_path) = metadata["proxyPath"].as_str().filter(|value| !value.is_empty()) {
                        let proxy_request = json!({"command":"generate_proxy", "path":source, "proxyPath":proxy_path});
                        let _ = proxy_tasks.send((id | (1u64 << 63), proxy_request));
                    }
                }
                result
            }
            "export_project" => export_project(&request, id),
            "" => Err("Background command is required".into()),
            other => Err(format!("Unsupported background command: {other}")),
        };
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

fn preview_worker(tasks: Receiver<(u64, Value)>, events: Sender<String>, preview_inflight: Arc<AtomicBool>) {
    /* The preview process and its pipe are owned exclusively by this thread.
       That gives FFmpeg one clear owner and keeps decode waits off the UI and
       media-import/export workers. */
    let mut engine = PreviewEngine::default();
    while let Ok((id, request)) = tasks.recv() {
        let queued_ms = request["submittedAtMs"].as_u64().unwrap_or(0);
        let now_ms = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_millis() as u64;
        let queue_ms = now_ms.saturating_sub(queued_ms);
        let total_started = Instant::now();
        let (result, setup_ms, decode_ms) = engine.render(&request, id);
        preview_inflight.store(false, Ordering::Release);
        let response = match result {
            Ok((width, height, at)) => json!({"requestId":id,"frameId":id,"command":"preview_frame","ok":true,
                "width":width,"height":height,"at":at,"queueMs":queue_ms,
                "streamSetupMs":setup_ms,"decodeWaitMs":decode_ms,
                "totalMs":total_started.elapsed().as_secs_f64()*1000.0}),
            Err(error) => json!({"requestId":id,"command":"preview_frame","ok":false,"error":error,
                "queueMs":queue_ms,"streamSetupMs":setup_ms,"decodeWaitMs":decode_ms,
                "totalMs":total_started.elapsed().as_secs_f64()*1000.0}),
        };
        if events.send(response.to_string()).is_err() { break; }
    }
}

fn proxy_worker(tasks: Receiver<(u64, Value)>, events: Sender<String>) {
    while let Ok((id, request)) = tasks.recv() {
        let source = request["path"].as_str().unwrap_or_default();
        let proxy = request["proxyPath"].as_str().unwrap_or_default();
        let result = (|| -> Result<Value, String> {
            if source.is_empty() || proxy.is_empty() { return Err("Proxy source and destination are required".into()); }
            let destination = std::path::Path::new(proxy);
            if destination.is_file() { return Ok(json!({"proxyPath": proxy, "cached": true})); }
            if !std::path::Path::new(source).is_file() { return Err(format!("Media file is missing: {source}")); }
            let ffmpeg = multimedia_binary("ffmpeg");
            let temporary = destination.with_file_name(format!("proxy-render-{id}.mp4"));
            let mut command = media_command(&ffmpeg);
            command.args(["-hide_banner", "-loglevel", "error", "-i"]).arg(source)
                .args(["-vf", "scale='min(1280,iw)':'min(720,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2",
                       "-an", "-threads", "1", "-c:v", "libx264", "-preset", "ultrafast", "-crf", "30",
                       "-g", "6", "-keyint_min", "6", "-sc_threshold", "0", "-bf", "0",
                       "-pix_fmt", "yuv420p", "-movflags", "+faststart", "-y"])
                .arg(&temporary);
            let output = command.output().map_err(|e| format!("Could not start FFmpeg proxy encode: {e}"))?;
            if !output.status.success() {
                let _ = std::fs::remove_file(&temporary);
                return Err(ffmpeg_error(output));
            }
            if !destination.exists() {
                std::fs::rename(&temporary, destination).map_err(|e| format!("Could not finalize media proxy: {e}"))?;
            } else {
                let _ = std::fs::remove_file(&temporary);
            }
            Ok(json!({"proxyPath": proxy, "cached": false}))
        })();
        let response = match result {
            Ok(value) => {
                let mut fields = value.as_object().cloned().unwrap_or_default();
                fields.insert("requestId".into(), json!(id));
                fields.insert("command".into(), json!("generate_proxy"));
                fields.insert("ok".into(), json!(true));
                Value::Object(fields)
            }
            Err(error) => json!({"requestId": id, "command": "generate_proxy", "ok": false, "error": error}),
        };
        if events.send(response.to_string()).is_err() { break; }
    }
}

fn media_content_hash(path: &std::path::Path) -> Result<u64, String> {
    use std::io::Read;
    let mut file = std::fs::File::open(path).map_err(|e| format!("Could not read media for cache: {e}"))?;
    let mut hasher = DefaultHasher::new();
    let mut block = [0u8; 64 * 1024];
    loop {
        let read = file.read(&mut block).map_err(|e| format!("Could not hash media: {e}"))?;
        if read == 0 { break; }
        block[..read].hash(&mut hasher);
    }
    Ok(hasher.finish())
}

fn inspect_media(path: &str) -> Result<Value, String> {
    use std::{
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
    let media_hash = media_content_hash(path)?;
    let cache_dir = std::env::temp_dir().join("pluto-media-cache");
    let _ = std::fs::create_dir_all(&cache_dir);
    let waveform = cache_dir.join(format!("{media_hash:016x}-wave-v5.png"));
    let thumbnail = cache_dir.join(format!("{media_hash:016x}-thumb-v2.jpg"));
    let proxy = if kind == "video" {
        cache_dir.join(format!("{media_hash:016x}-proxy-720p-g6-v3.mp4"))
    } else { std::path::PathBuf::new() };
    if kind == "video" { remember_media_proxy(&path.to_string_lossy(), proxy.clone()); }
    let ffmpeg = multimedia_binary("ffmpeg");
    let waveform_path = if has_audio {
        let generated = if waveform.is_file() {
            true
        } else {
            media_command(&ffmpeg)
                .args(["-v", "error", "-i"])
                .arg(path)
                .args(["-filter_complex", "showwavespic=s=1024x256:colors=0x9b91b0:scale=sqrt", "-frames:v", "1", "-y"])
                .arg(&waveform)
                .status()
                .is_ok_and(|status| status.success())
        };
        generated.then(|| waveform.to_string_lossy().into_owned())
    } else { None };
    let thumbnail_path = if kind == "audio" {
        None
    } else {
        let generated = if thumbnail.metadata().is_ok_and(|meta| meta.len() > 128) {
            true
        } else {
            let _ = std::fs::remove_file(&thumbnail);
            let mut command = media_command(&ffmpeg);
            command.args(["-v", "error"]);
            if kind == "video" {
                let seek = format!("{:.3}", (duration * 0.1).min(2.0));
                command.args(["-ss", seek.as_str()]);
            }
            command.arg("-i").arg(path)
                .args(["-frames:v", "1", "-vf", "scale=320:180:force_original_aspect_ratio=decrease,pad=320:180:(ow-iw)/2:(oh-ih)/2:color=black", "-q:v", "8", "-y"])
                .arg(&thumbnail);
            let first_attempt = command.status().is_ok_and(|status| status.success())
                && thumbnail.metadata().is_ok_and(|meta| meta.len() > 128);
            if first_attempt {
                true
            } else {
                let _ = std::fs::remove_file(&thumbnail);
                let mut fallback = media_command(&ffmpeg);
                fallback.args(["-v", "error", "-i"]).arg(path)
                    .args(["-frames:v", "1", "-vf", "scale=320:180:force_original_aspect_ratio=decrease,pad=320:180:(ow-iw)/2:(oh-ih)/2:color=black", "-q:v", "8", "-y"])
                    .arg(&thumbnail)
                    .status()
                    .is_ok_and(|status| status.success())
                    && thumbnail.metadata().is_ok_and(|meta| meta.len() > 128)
            }
        };
        generated.then(|| thumbnail.to_string_lossy().into_owned())
    };
    Ok(json!({
        "path": path.to_string_lossy(), "name": name, "duration": duration,
        "kind": kind, "hasAudio": has_audio,
        "proxyPath": proxy.to_string_lossy(),
        "filmstripPath": Value::Null, "waveformPath": waveform_path, "thumbnailPath": thumbnail_path
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

#[derive(Clone)]
struct RenderInput { path: String, image: bool, seek: f64 }

fn render_inputs(timeline: &Timeline, deduplicate: bool) -> Result<(Vec<RenderInput>, HashMap<String, usize>), String> {
    let mut clips: Vec<_> = timeline.clips.iter().filter(|clip| clip.extra.get("path").and_then(Value::as_str).is_some()).collect();
    clips.sort_by(|a, b| {
        let order = |track: TrackId| match track { TrackId::V1 => 0, TrackId::V2 => 1, TrackId::V3 => 2, TrackId::V4 => 3, TrackId::A1 => 4, TrackId::A2 => 5 };
        order(a.track).cmp(&order(b.track)).then_with(|| a.start.total_cmp(&b.start))
    });
    let mut inputs: Vec<RenderInput> = Vec::with_capacity(clips.len());
    let mut indices = HashMap::with_capacity(clips.len());
    let mut path_indices = HashMap::with_capacity(clips.len());
    for clip in clips {
        let path = clip.extra.get("path").and_then(Value::as_str).unwrap_or_default();
        if !std::path::Path::new(&path).is_file() { return Err(format!("Media file is missing: {path}")); }
        if deduplicate {
            if let Some(&input_index) = path_indices.get(path) {
                indices.insert(clip.id.clone(), input_index);
                continue;
            }
        }
        let image = clip.extra.get("kind").and_then(Value::as_str) == Some("image");
        let input_index = inputs.len();
        indices.insert(clip.id.clone(), input_index);
        if deduplicate { path_indices.insert(path.to_owned(), input_index); }
        let seek = clip.extra.get("previewSeek").and_then(Value::as_f64).unwrap_or(0.0).max(0.0);
        inputs.push(RenderInput { path: path.to_owned(), image, seek });
    }
    Ok((inputs, indices))
}

fn project_duration(timeline: &Timeline) -> f64 {
    timeline.clips.iter().map(Clip::end).fold(0.0, f64::max)
}

fn transition_out(clip: &Clip) -> Option<(String, f64)> {
    let transition = &clip.extra["props"]["transitionOut"];
    let stored_kind = transition["type"].as_str()?;
    let kind = match stored_kind {
        "none" => return None,
        "crossfade" => "fade",       // projects saved by older Stella builds
        "dip" => "fadeblack",
        "wipe" => "wipeleft",
        "iris" => "circleopen",
        "slide" => "slideleft",
        value => value,
    };
    if !matches!(kind,
        "fade" | "dissolve" | "fadeblack" | "fadewhite" |
        "wipeleft" | "wiperight" | "wipeup" | "wipedown" |
        "diagtl" | "diagtr" | "diagbl" | "diagbr" |
        "slideleft" | "slideright" | "slideup" | "slidedown" |
        "circleopen" | "circleclose" | "radial" | "zoomin" | "squeezeh" | "squeezev")
    { return None; }
    let duration = transition["duration"].as_f64().filter(|v| v.is_finite()).unwrap_or(1.0).max(0.05).min(clip.duration.min(3.0).max(0.05));
    Some((kind.to_owned(), duration))
}

fn transition_between(left: &Clip, right: &Clip) -> Option<(String, f64)> {
    if left.track != right.track || (left.end() - right.start).abs() > 0.04 { return None; }
    let (kind, duration) = transition_out(left)?;
    let preserve_preview_duration = left.extra.get("previewSeekAdjusted").and_then(Value::as_bool) == Some(true);
    let duration = if preserve_preview_duration { duration } else { duration.min(left.duration * 0.5).min(right.duration * 0.5) };
    (duration > 0.0).then_some((kind, duration))
}

fn timeline_transition_start_at(timeline: &Timeline, at: f64) -> Option<f64> {
    let mut clips: Vec<_> = timeline.clips.iter()
        .filter(|clip| clip.track.is_video() && clip.extra.get("kind").and_then(Value::as_str) != Some("audio"))
        .collect();
    clips.sort_by(|a, b| {
        let rank = |track: TrackId| match track { TrackId::V1 => 0, TrackId::V2 => 1, TrackId::V3 => 2, TrackId::V4 => 3, TrackId::A1 => 4, TrackId::A2 => 5 };
        rank(a.track).cmp(&rank(b.track)).then_with(|| a.start.total_cmp(&b.start))
    });
    clips.windows(2).find_map(|pair| {
        let (_, duration) = transition_between(pair[0], pair[1])?;
        let start = pair[1].start - duration;
        (at >= start - 0.02 && at <= pair[1].start + 0.02).then_some(start.max(0.0))
    })
}

fn split_source_streams(
    graph: &mut Vec<String>,
    clips: &[&Clip],
    indices: &HashMap<String, usize>,
    video: bool,
    prefix: &str,
) -> HashMap<String, String> {
    let mut by_input: HashMap<usize, Vec<&Clip>> = HashMap::new();
    for clip in clips {
        if let Some(&input) = indices.get(&clip.id) { by_input.entry(input).or_default().push(*clip); }
    }
    let stream = if video { "v" } else { "a" };
    let splitter = if video { "split" } else { "asplit" };
    let mut labels_by_clip = HashMap::with_capacity(clips.len());
    for (input, uses) in by_input {
        let labels: Vec<_> = uses.iter().enumerate()
            .map(|(ordinal, clip)| format!("{prefix}_{}_{}", clip.id.replace(|c: char| !c.is_ascii_alphanumeric(), "_"), ordinal))
            .collect();
        if labels.len() == 1 {
            graph.push(format!("[{input}:{stream}]null[{}]", labels[0]));
        } else {
            graph.push(format!("[{input}:{stream}]{splitter}={}[{}]", labels.len(), labels.join("][")));
        }
        for (clip, label) in uses.into_iter().zip(labels) { labels_by_clip.insert(clip.id.clone(), label); }
    }
    labels_by_clip
}

fn export_filter_graph(timeline: &Timeline, width: u32, height: u32, fps: u32) -> Result<(String, Vec<RenderInput>, f64, bool), String> {
    export_filter_graph_mode(timeline, width, height, fps, true)
}

fn export_filter_graph_mode(timeline: &Timeline, width: u32, height: u32, fps: u32, deduplicate: bool) -> Result<(String, Vec<RenderInput>, f64, bool), String> {
    let duration = project_duration(timeline);
    if duration <= 0.0 { return Err("Add at least one clip before exporting".into()); }
    let (inputs, indices) = render_inputs(timeline, deduplicate)?;
    let mut graph = vec![format!("color=c=black:s={width}x{height}:r={fps}:d={duration:.5},format=rgba[base0]")];
    let mut video_clips: Vec<_> = timeline.clips.iter().filter(|clip| clip.track.is_video() && clip.extra.get("kind").and_then(Value::as_str) != Some("audio") && indices.contains_key(&clip.id)).collect();
    video_clips.sort_by(|a, b| {
        let order = |track: TrackId| match track { TrackId::V1 => 0, TrackId::V2 => 1, TrackId::V3 => 2, TrackId::V4 => 3, TrackId::A1 => 4, TrackId::A2 => 5 };
        order(a.track).cmp(&order(b.track)).then_with(|| a.start.total_cmp(&b.start))
    });
    let mut outgoing = Vec::with_capacity(video_clips.len());
    for i in 0..video_clips.len() {
        outgoing.push(if i + 1 < video_clips.len() {
            transition_between(video_clips[i], video_clips[i + 1])
        } else { None });
    }
    let mut incoming = vec![None; video_clips.len()];
    for i in 1..video_clips.len() { incoming[i] = outgoing[i - 1].clone(); }
    let clip_streams = split_source_streams(&mut graph, &video_clips, &indices, true, "vsrc");
    let mut video_branches: Vec<(String, Option<String>, Option<String>)> = Vec::with_capacity(video_clips.len());
    for (i, clip) in video_clips.iter().enumerate() {
        let source = &clip_streams[&clip.id];
        let has_incoming = incoming[i].is_some();
        let has_outgoing = outgoing[i].is_some();
        let mut labels = Vec::with_capacity(1 + usize::from(has_incoming) + usize::from(has_outgoing));
        labels.push(format!("v{i}_body_src"));
        if has_incoming { labels.push(format!("v{i}_in_src")); }
        if has_outgoing { labels.push(format!("v{i}_out_src")); }
        if labels.len() == 1 {
            graph.push(format!("[{source}]null[{}]", labels[0]));
        } else {
            graph.push(format!("[{source}]split={}[{}]", labels.len(), labels.join("][")));
        }
        let mut at = 1;
        let incoming_source = if has_incoming { let label = labels[at].clone(); at += 1; Some(label) } else { None };
        let outgoing_source = if has_outgoing { Some(labels[at].clone()) } else { None };
        video_branches.push((labels[0].clone(), incoming_source, outgoing_source));
    }
    let mut body_layers: Vec<Option<(String, String, String)>> = vec![None; video_clips.len()];
    for (i, clip) in video_clips.iter().enumerate() {
        let value = serde_json::to_value(clip).unwrap_or_default();
        let (filters, x, y) = filters::video_filters(&value, width, height, fps, clip.start, clip.duration, None, None);
        let label = format!("vbody{i}");
        graph.push(format!("[{}]{filters}[{label}]", video_branches[i].0));
        body_layers[i] = Some((label, x, y));
    }

    let mut transition_layers = vec![None; video_clips.len()];
    for i in 0..video_clips.len().saturating_sub(1) {
        let Some((kind, duration)) = outgoing[i].as_ref() else { continue; };
        let left = video_clips[i];
        let right = video_clips[i + 1];
        let transition_start = right.start - duration;
        let left_source = left.extra.get("sourceOffset").and_then(Value::as_f64).unwrap_or(0.0) + left.duration - duration;
        let right_source = right.extra.get("sourceOffset").and_then(Value::as_f64).unwrap_or(0.0);
        let mut left_value = serde_json::to_value(left).map_err(|e| e.to_string())?;
        let mut right_value = serde_json::to_value(right).map_err(|e| e.to_string())?;
        left_value["sourceOffset"] = json!(left_source);
        right_value["sourceOffset"] = json!(right_source);
        let (left_filters, left_x, left_y) = filters::video_filters(&left_value, width, height, fps, 0.0, *duration, None, None);
        let (right_filters, right_x, right_y) = filters::video_filters(&right_value, width, height, fps, 0.0, *duration, None, None);
        let left_raw = format!("tx{i}a_raw");
        let right_raw = format!("tx{i}b_raw");
        let left_full = format!("tx{i}a");
        let right_full = format!("tx{i}b");
        let output = format!("tx{i}_out");
        let left_bg = format!("tx{i}a_bg");
        let right_bg = format!("tx{i}b_bg");
        graph.push(format!("[{}]{left_filters},fps={fps},settb=AVTB,format=rgba[{left_raw}]", video_branches[i].2.as_ref().unwrap()));
        graph.push(format!("[{}]{right_filters},fps={fps},settb=AVTB,format=rgba[{right_raw}]", video_branches[i + 1].1.as_ref().unwrap()));
        graph.push(format!("color=c=black@0:s={width}x{height}:r={fps}:d={duration:.5},format=rgba,settb=AVTB[{left_bg}]"));
        graph.push(format!("color=c=black@0:s={width}x{height}:r={fps}:d={duration:.5},format=rgba,settb=AVTB[{right_bg}]"));
        graph.push(format!("[{left_bg}][{left_raw}]overlay=x={left_x}:y={left_y}:shortest=1:repeatlast=0:format=auto,format=rgba,setpts=PTS-STARTPTS[{left_full}]"));
        graph.push(format!("[{right_bg}][{right_raw}]overlay=x={right_x}:y={right_y}:shortest=1:repeatlast=0:format=auto,format=rgba,setpts=PTS-STARTPTS[{right_full}]"));
        graph.push(format!("[{left_full}][{right_full}]xfade=transition={kind}:duration={duration:.5}:offset=0,format=rgba,setpts=PTS-STARTPTS+{transition_start:.5}/TB[{output}]"));
        transition_layers[i] = Some(output);
    }

    let mut base = "base0".to_owned();
    let mut layer_index = 0;
    for i in 0..video_clips.len() {
        if let Some((label, x, y)) = body_layers[i].as_ref() {
            let next = format!("base{}", layer_index + 1);
            graph.push(format!("[{base}][{label}]overlay=x={x}:y={y}:eval=frame:eof_action=pass:repeatlast=0:format=auto[{next}]"));
            base = next;
            layer_index += 1;
        }
        if let Some(label) = transition_layers[i].as_ref() {
            let next = format!("base{}", layer_index + 1);
            graph.push(format!("[{base}][{label}]overlay=x=0:y=0:eof_action=pass:repeatlast=0:format=auto[{next}]"));
            base = next;
            layer_index += 1;
        }
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
    for input in inputs {
        if input.image { command.args(["-loop", "1", "-framerate", &fps.to_string()]); }
        command.arg("-i").arg(input.path);
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

fn preview_signature(timeline: &Timeline, width: u32, height: u32, fps: u32) -> Result<u64, String> {
    let mut hasher = DefaultHasher::new();
    "stella-preview-stream-v2".hash(&mut hasher);
    serde_json::to_vec(timeline).map_err(|e| e.to_string())?.hash(&mut hasher);
    width.hash(&mut hasher); height.hash(&mut hasher); fps.hash(&mut hasher);
    Ok(hasher.finish())
}

fn start_preview_stream(timeline: &Timeline, width: u32, height: u32, fps: u32, id: u64, signature: u64, origin_index: u64) -> Result<PreviewStream, String> {
    let (graph, inputs, duration, _) = export_filter_graph_mode(timeline, width, height, fps, false)?;
    let script = filter_script(id, &graph)?;
    let ffmpeg = multimedia_binary("ffmpeg");
    let mut command = media_command(&ffmpeg);
    /* Let FFmpeg size filter/decoder workers for the host CPU rather than
       pinning every preview graph to two threads. Video inputs use FFmpeg's
       automatic hardware decoder selection (e.g. D3D11VA on supported Windows
       GPUs); unsupported codecs/drivers remain eligible for software decode. */
    command.args(["-hide_banner", "-loglevel", "error"]);
    append_filter_graph_arg(&mut command, &ffmpeg, &script);
    for input in inputs {
        if input.image {
            command.args(["-loop", "1", "-framerate", &fps.to_string()]);
        } else {
            if input.seek > 0.0001 {
                command.args(["-accurate_seek", "-ss", &format!("{:.6}", input.seek)]);
            }
            command.args(["-hwaccel", "auto", "-threads", "0"]);
        }
        command.arg("-i").arg(input.path);
    }
    command.args(["-map", "[vout]", "-an", "-t", &format!("{duration:.5}"),
        "-r", &fps.to_string(), "-fps_mode", "cfr", "-s", &format!("{width}x{height}"),
        "-pix_fmt", "rgba", "-flush_packets", "1", "-f", "rawvideo", "pipe:1"])
        .stdout(Stdio::piped()).stderr(Stdio::null()).stdin(Stdio::null());
    let mut child = command.spawn().map_err(|error| {
        let _ = std::fs::remove_file(&script);
        format!("Could not start FFmpeg preview stream: {error}")
    })?;
    let Some(mut stdout) = child.stdout.take() else {
        let _ = child.kill(); let _ = child.wait(); let _ = std::fs::remove_file(&script);
        return Err("FFmpeg preview stream has no output pipe".into());
    };

    // FFmpeg decodes independently of the UI/request thread. The bounded queue
    // keeps a small amount of decoded video ready without allowing RAM usage to
    // grow with the duration of the project.
    let child = Arc::new(Mutex::new(child));
    let reader_child = Arc::clone(&child);
    let frame_bytes = width as usize * height as usize * 4;
    let (frame_tx, frame_rx) = mpsc::sync_channel::<Result<(u64, Vec<u8>), String>>(PREVIEW_DECODE_QUEUE_DEPTH);
    let (recycle_tx, recycle_rx) = mpsc::sync_channel::<Vec<u8>>(PREVIEW_DECODE_QUEUE_DEPTH + 1);
    let (stop_tx, stop_rx) = mpsc::channel::<()>();
    let decoder_thread = match thread::Builder::new()
        .name("stella-preview-decode".into())
        .spawn(move || {
            let mut frame_index = 0u64;
            loop {
                if stop_rx.try_recv().is_ok() { break; }

                // Reuse buffers returned by the consumer. Allocations therefore
                // settle after the queue is warm instead of allocating several
                // megabytes for every decoded frame.
                let mut pixels = recycle_rx.try_recv()
                    .unwrap_or_else(|_| vec![0; frame_bytes]);
                if let Err(error) = stdout.read_exact(&mut pixels) {
                    let status = reader_child.lock().ok()
                        .and_then(|mut child| child.try_wait().ok().flatten())
                        .map(|status| status.to_string());
                    let message = if let Some(status) = status {
                        format!("FFmpeg preview stream ended before frame {frame_index} ({status})")
                    } else {
                        format!("Could not read preview frame {frame_index}: {error}")
                    };
                    let _ = frame_tx.send(Err(message));
                    break;
                }
                if frame_tx.send(Ok((frame_index, pixels))).is_err() { break; }
                frame_index += 1;
            }
        }) {
        Ok(worker) => worker,
        Err(error) => {
            if let Ok(mut child) = child.lock() { let _ = child.kill(); let _ = child.wait(); }
            let _ = std::fs::remove_file(&script);
            return Err(format!("Could not start preview decoder thread: {error}"));
        }
    };

    Ok(PreviewStream {
        signature, width, height, fps, origin_index, child, script, next_index: 0,
        last_index: None, pixels: vec![0; frame_bytes], frames: Some(frame_rx),
        recycle: recycle_tx, stop: stop_tx, decoder_thread: Some(decoder_thread),
    })
}

impl PreviewEngine {
    fn render(&mut self, request: &Value, id: u64) -> (Result<(u32, u32, f64), String>, f64, f64) {
        let mut setup_ms = 0.0;
        let mut decode_ms = 0.0;
        let result = (|| -> Result<(u32, u32, f64), String> {
            let timeline: Timeline = serde_json::from_value(request["timeline"].clone()).map_err(|e| format!("Invalid timeline: {e}"))?;
            let at = request["at"].as_f64().filter(|value| value.is_finite() && *value >= 0.0).unwrap_or(0.0);
            let height = request["height"].as_u64().unwrap_or(720).clamp(240, 1080) as u32;
            let width = ((height as f64 * 16.0 / 9.0).round() as u32) & !1;
            let fps = request["fps"].as_u64().unwrap_or(30).clamp(1, 120) as u32;
            let target_index = (at * fps as f64).round() as u64;
            let frame_at = target_index as f64 / fps as f64;

            let mut preview_timeline = timeline;
            preview_timeline.clips.retain(|clip| clip.track.is_video() && clip.extra.get("kind").and_then(Value::as_str) != Some("audio"));
            for clip in &mut preview_timeline.clips {
                let original = clip.extra.get("path").and_then(Value::as_str).unwrap_or_default().to_owned();
                let source = preview_source_path(clip);
                if !std::path::Path::new(&source).is_file() { return Err(format!("Media file is missing: {source}")); }
                clip.extra.insert("path".into(), Value::String(source));
                clip.extra.insert("hasAudio".into(), Value::Bool(false));
                if original.is_empty() { return Err("Video clip has no source path".into()); }
            }
            if preview_timeline.clips.is_empty() {
                self.stream = None;
                publish_black_frame(id, width, height, frame_at)?;
                return Ok((width, height, frame_at));
            }
            if frame_at >= project_duration(&preview_timeline) {
                self.stream = None;
                publish_black_frame(id, width, height, frame_at)?;
                return Ok((width, height, frame_at));
            }
            let signature = preview_signature(&preview_timeline, width, height, fps)?;
            let transition_start = timeline_transition_start_at(&preview_timeline, frame_at);
            let restart = self.stream.as_ref().is_none_or(|stream| stream.signature != signature ||
                stream.width != width || stream.height != height || stream.fps != fps ||
                (target_index < stream.origin_index + stream.next_index &&
                    stream.last_index.map(|index| stream.origin_index + index) != Some(target_index)));
            if restart {
                self.stream = None;
                let setup_started = Instant::now();
                /* Rebuild at the beginning of the active transition. Starting at
                   timeline zero made a seek into a late transition decode the
                   entire project again. */
                let origin_index = transition_start
                    .map(|start| (start * fps as f64).floor().max(0.0) as u64)
                    .unwrap_or(target_index);
                let origin_time = origin_index as f64 / fps as f64;
                let mut shifted = preview_timeline.clone();
                shifted.clips.retain(|clip| clip.end() > origin_time);
                for clip in &mut shifted.clips {
                    let old_start = clip.start;
                    let old_end = clip.end();
                    let old_offset = clip.extra.get("sourceOffset").and_then(Value::as_f64).unwrap_or(0.0);
                    let elapsed = (origin_time - old_start).max(0.0);
                    let seek = old_offset + elapsed;
                    clip.start = (old_start - origin_time).max(0.0);
                    clip.duration = old_end - old_start.max(origin_time);
                    clip.extra.insert("previewSeek".into(), json!(seek));
                    clip.extra.insert("sourceOffset".into(), json!(0.0));
                    if origin_time > 0.0 { clip.extra.insert("previewSeekAdjusted".into(), json!(true)); }
                    else { clip.extra.remove("previewSeekAdjusted"); }
                }
                self.stream = Some(start_preview_stream(&shifted, width, height, fps, id, signature, origin_index)?);
                setup_ms = setup_started.elapsed().as_secs_f64() * 1000.0;
            }
            let stream = self.stream.as_mut().ok_or("Preview stream is unavailable")?;
            let local_target_index = target_index.saturating_sub(stream.origin_index);

            // Check whether the requested frame is already the current frame
            // BEFORE consuming the decoder queue. Checking after decoding made
            // every newly decoded frame look reusable and skipped publication.
            let reuse_last = stream.last_index == Some(local_target_index);
            let decode_started = Instant::now();
            if !reuse_last {
                let frames = stream.frames.as_ref().ok_or("Preview decoder queue is unavailable")?;
                while stream.next_index <= local_target_index {
                    let decoded = frames.recv()
                        .map_err(|_| "Preview decoder stopped unexpectedly".to_owned())??;
                    let (frame_index, mut pixels) = decoded;
                    if pixels.len() != stream.pixels.len() {
                        return Err("FFmpeg returned a malformed preview frame".into());
                    }
                    stream.next_index = frame_index.saturating_add(1);
                    if frame_index == local_target_index {
                        // The old reusable buffer goes back to the decoder;
                        // the selected frame becomes the stream's current frame.
                        std::mem::swap(&mut stream.pixels, &mut pixels);
                        let _ = stream.recycle.try_send(pixels);
                        stream.last_index = Some(frame_index);
                    } else {
                        // Frames skipped because the playhead advanced are
                        // recycled immediately instead of being uploaded late.
                        let _ = stream.recycle.try_send(pixels);
                    }
                }
            }
            decode_ms = decode_started.elapsed().as_secs_f64() * 1000.0;
            publish_preview_buffer(id, width, height, frame_at, &mut stream.pixels, reuse_last)?;
            Ok((width, height, frame_at))
        })();
        if result.is_err() { self.stream = None; }
        (result, setup_ms, decode_ms)
    }
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
                    request["sourceDuration"].as_f64(),
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
            set_timeline_clip_property(&mut timeline, id, path, request["value"].clone())?;
            save_history(backend, before)?;
            serde_json::to_value(timeline.clone()).map_err(|e| e.to_string())
        }
        "timeline_set_clip_properties" => {
            let before = timeline.clone();
            let id = request["id"].as_str().ok_or("Clip id is required")?;
            let updates = request["properties"].as_array().ok_or("Property updates are required")?;
            let selected_index = timeline.clips.iter().position(|clip| clip.id == id)
                .ok_or_else(|| format!("Clip {id} was not found"))?;
            let group_id = timeline.clips[selected_index].extra.get("groupId")
                .and_then(Value::as_str).map(str::to_owned);
            let indices: Vec<_> = group_id.as_deref().map(|group| timeline.clips.iter().enumerate()
                .filter_map(|(index, clip)| (clip.extra.get("groupId").and_then(Value::as_str) == Some(group)).then_some(index))
                .collect()).unwrap_or_else(|| vec![selected_index]);
            let mut working: Vec<_> = indices.iter().map(|&index| (index, timeline.clips[index].clone())).collect();
            for update in updates {
                let path = update["path"].as_str().ok_or("Property path is required")?;
                let shared = is_group_shared_property(path) && group_id.is_some();
                for (index, clip) in &mut working {
                    if *index == selected_index || shared {
                        set_clip_property(clip, path, update["value"].clone())?;
                    }
                }
            }
            for (index, clip) in working { timeline.clips[index] = clip; }
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

fn is_group_shared_property(path: &str) -> bool {
    path.starts_with("props.effects.") || path.starts_with("props.colors.")
}

fn set_timeline_clip_property(timeline: &mut Timeline, id: &str, path: &str, value: Value) -> Result<(), String> {
    let selected = timeline.clips.iter().position(|clip| clip.id == id)
        .ok_or_else(|| format!("Clip {id} was not found"))?;
    let group_id = timeline.clips[selected].extra.get("groupId").and_then(Value::as_str).map(str::to_owned);
    let indices: Vec<_> = if is_group_shared_property(path) {
        group_id.as_deref().map(|group| timeline.clips.iter().enumerate()
            .filter_map(|(index, clip)| (clip.extra.get("groupId").and_then(Value::as_str) == Some(group)).then_some(index))
            .collect()).unwrap_or_else(|| vec![selected])
    } else { vec![selected] };
    let mut updated = Vec::with_capacity(indices.len());
    for index in indices {
        let mut clip = timeline.clips[index].clone();
        set_clip_property(&mut clip, path, value.clone())?;
        updated.push((index, clip));
    }
    for (index, clip) in updated { timeline.clips[index] = clip; }
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
            let now_ms = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_millis() as u64;
            request["submittedAtMs"] = json!(now_ms);
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
        let sender = if is_preview { &backend.preview_task_tx }
            else if command == "generate_proxy" { &backend.proxy_task_tx }
            else { &backend.task_tx };
        if sender.send((id, request)).is_ok() {
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

/// Copies the newest raw preview frame into caller-owned memory. Returns 1 on
/// success, -1 when the buffer is too small (required length is still written),
/// and 0 when this frame is no longer available.
#[no_mangle]
pub unsafe extern "C" fn pluto_backend_copy_preview_frame(
    request_id: u64,
    destination: *mut u8,
    capacity: usize,
    width: *mut u32,
    height: *mut u32,
    byte_len: *mut usize,
    at: *mut f64,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if width.is_null() || height.is_null() || byte_len.is_null() || at.is_null() { return 0; }
        let Some(slot) = PREVIEW_FRAME.get() else { return 0; };
        let Ok(slot) = slot.lock() else { return 0; };
        if slot.request_id != request_id || slot.pixels.is_empty() { return 0; }
        *width = slot.width;
        *height = slot.height;
        *byte_len = slot.pixels.len();
        *at = slot.at;
        if destination.is_null() || capacity < slot.pixels.len() { return -1; }
        std::ptr::copy_nonoverlapping(slot.pixels.as_ptr(), destination, slot.pixels.len());
        1
    })).unwrap_or(0)
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
    fn preview_publication_stores_the_first_decoded_frame() {
        let request_id = 0x5354454c4c41;
        let mut pixels = vec![10, 20, 30, 255];
        publish_preview_buffer(request_id, 1, 1, 0.0, &mut pixels, true).unwrap();

        let slot = PREVIEW_FRAME.get().unwrap().lock().unwrap();
        assert_eq!(slot.request_id, request_id);
        assert_eq!(slot.width, 1);
        assert_eq!(slot.height, 1);
        assert_eq!(slot.pixels, vec![10, 20, 30, 255]);
    }

    #[test]
    fn ffi_dispatch_returns_structured_error_for_unknown_command() {
        let error = dispatch(json!({"command":"unknown"})).unwrap_err();
        assert!(error.contains("Unsupported command"));
    }
}
