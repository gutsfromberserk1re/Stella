//! Downloadable Whisper model catalog and per-user model storage.
//!
//! Model files are never bundled with Stella. The downloader writes to a
//! temporary `.part` file, supports HTTP range continuation, verifies SHA-256,
//! then atomically renames the verified model into the user model directory.

use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc::Sender,
        Arc,
    },
    time::{Duration, Instant},
};

const HF_REVISION: &str = "0b364b566045a405be7225ee1e415a073e04da77";
const HF_BASE: &str = "https://huggingface.co/ggerganov/whisper.cpp/resolve";
const CHUNK_BYTES: usize = 256 * 1024;

#[derive(Clone, Copy)]
pub struct ModelSpec {
    pub tier: &'static str,
    pub label: &'static str,
    pub file_name: &'static str,
    pub sha256: &'static str,
}

pub const MODELS: [ModelSpec; 3] = [
    ModelSpec {
        tier: "performance",
        label: "Performance",
        file_name: "ggml-base-q8_0.bin",
        sha256: "c577b9a86e7e048a0b7eada054f4dd79a56bbfa911fbdacf900ac5b567cbb7d9",
    },
    ModelSpec {
        tier: "balanced",
        label: "Balanced",
        file_name: "ggml-small-q8_0.bin",
        sha256: "49c8fb02b65e6049d5fa6c04f81f53b867b5ec9540406812c643f177317f779f",
    },
    ModelSpec {
        tier: "quality",
        label: "Quality",
        file_name: "ggml-medium-q5_0.bin",
        sha256: "19fea4b380c3a618ec4723c3eef2eb785ffba0d0538cf43f8f235e7b3b34220f",
    },
];

pub fn model_for_tier(tier: &str) -> Option<&'static ModelSpec> {
    MODELS.iter().find(|model| model.tier == tier)
}

pub fn model_directory() -> Result<PathBuf, String> {
    #[cfg(target_os = "windows")]
    let root = std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .ok_or_else(|| "Windows did not provide LOCALAPPDATA".to_owned())?;

    #[cfg(target_os = "macos")]
    let root = std::env::var_os("HOME")
        .map(PathBuf::from)
        .ok_or_else(|| "HOME is not available".to_owned())?
        .join("Library")
        .join("Application Support");

    #[cfg(all(unix, not(target_os = "macos")))]
    let root = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".local").join("share")))
        .ok_or_else(|| "No user data directory is available".to_owned())?;

    Ok(root.join("Stella").join("Models"))
}

fn model_path(directory: &Path, model: &ModelSpec) -> PathBuf {
    directory.join(model.file_name)
}

fn file_has_expected_hash(path: &Path, expected: &str) -> Result<bool, String> {
    if !path.is_file() {
        return Ok(false);
    }
    let mut file = File::open(path).map_err(|e| format!("Could not open model file: {e}"))?;
    let mut hasher = Sha256::new();
    let mut buffer = [0u8; 1024 * 1024];
    loop {
        let count = file.read(&mut buffer).map_err(|e| format!("Could not verify model file: {e}"))?;
        if count == 0 { break; }
        hasher.update(&buffer[..count]);
    }
    let actual = format!("{:x}", hasher.finalize());
    Ok(actual.eq_ignore_ascii_case(expected))
}

pub fn model_status() -> Result<Value, String> {
    let directory = model_directory()?;
    let mut status = serde_json::Map::new();
    status.insert("directory".into(), json!(directory.to_string_lossy()));
    for model in MODELS.iter() {
        let path = model_path(&directory, model);
        // Files in the final location are only placed there after SHA-256
        // verification. Avoid hashing hundreds of MiB synchronously on the UI.
        let installed = path.is_file() && fs::metadata(&path).map(|meta| meta.len() > 0).unwrap_or(false);
        status.insert(format!("{}Installed", model.tier), json!(installed));
        status.insert(format!("{}Path", model.tier), json!(path.to_string_lossy()));
    }
    Ok(Value::Object(status))
}

fn emit(
    events: &Sender<String>,
    request_id: u64,
    tier: &str,
    state: &str,
    ok: bool,
    received: u64,
    total: Option<u64>,
    detail: Option<&str>,
) {
    let percent = total.filter(|total| *total > 0).map(|total| (received as f64 * 100.0 / total as f64).clamp(0.0, 100.0));
    let mut value = json!({
        "requestId": request_id,
        "command": "model_download",
        "tier": tier,
        "state": state,
        "ok": ok,
        "received": received,
        "total": total,
        "percent": percent,
    });
    if let Some(detail) = detail {
        value["detail"] = json!(detail);
    }
    let _ = events.send(value.to_string());
}

fn is_storage_full(error: &std::io::Error) -> bool {
    #[cfg(target_os = "windows")]
    { return matches!(error.raw_os_error(), Some(39) | Some(112)); }
    #[cfg(unix)]
    { return matches!(error.raw_os_error(), Some(28) | Some(122)); }
    #[allow(unreachable_code)]
    false
}

fn response_message(error: ureq::Error) -> String {
    match error {
        ureq::Error::Status(code, _response) => {
            format!("Model server returned HTTP {code}")
        }
        ureq::Error::Transport(error) => format!("Model download connection failed: {error}"),
    }
}

fn sha256_file(path: &Path) -> Result<String, String> {
    let mut file = File::open(path).map_err(|e| format!("Could not open downloaded model: {e}"))?;
    let mut hasher = Sha256::new();
    let mut buffer = [0u8; 1024 * 1024];
    loop {
        let count = file.read(&mut buffer).map_err(|e| format!("Could not read downloaded model: {e}"))?;
        if count == 0 { break; }
        hasher.update(&buffer[..count]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

fn download_response(agent: &ureq::Agent, url: &str, offset: u64) -> Result<ureq::Response, String> {
    let mut request = agent.get(url).set("User-Agent", "Stella-Video-Editor/0.1").set("Accept-Encoding", "identity");
    if offset > 0 {
        request = request.set("Range", &format!("bytes={offset}-"));
    }
    match request.call() {
        Ok(response) => Ok(response),
        Err(ureq::Error::Status(416, _)) if offset > 0 => {
            // A stale or oversized partial file cannot be resumed. Remove it
            // and restart cleanly rather than treating it as a permanent error.
            Err("RANGE_NOT_SATISFIABLE".into())
        }
        Err(error) => Err(response_message(error)),
    }
}

pub fn download_model(
    request_id: u64,
    tier: &str,
    cancel: Arc<AtomicBool>,
    events: &Sender<String>,
) -> Result<(), String> {
    let model = model_for_tier(tier).ok_or_else(|| format!("Unknown model tier: {tier}"))?;
    if cancel.load(Ordering::Acquire) {
        emit(events, request_id, tier, "cancelled", false, 0, None, Some("Download cancelled before it started."));
        return Ok(());
    }
    let directory = model_directory()?;
    fs::create_dir_all(&directory).map_err(|e| format!("Could not create model directory: {e}"))?;
    let destination = model_path(&directory, model);
    let partial = destination.with_file_name(format!("{}.part", model.file_name));

    if destination.is_file() {
        if file_has_expected_hash(&destination, model.sha256)? {
            emit(events, request_id, tier, "complete", true, fs::metadata(&destination).map(|m| m.len()).unwrap_or(0), None, Some("Already downloaded and verified"));
            return Ok(());
        }
        fs::remove_file(&destination).map_err(|e| format!("Could not remove invalid existing model: {e}"))?;
    }

    let url = format!("{HF_BASE}/{HF_REVISION}/{}", model.file_name);
    let agent = ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(20))
        .timeout_read(Duration::from_secs(20))
        .build();
    let mut offset = fs::metadata(&partial).map(|meta| meta.len()).unwrap_or(0);
    let response = match download_response(&agent, &url, offset) {
        Ok(response) => response,
        Err(error) if error == "RANGE_NOT_SATISFIABLE" => {
            // A partial file can be complete if the app was interrupted after
            // the final network write but before verification/rename.
            if partial.is_file() && file_has_expected_hash(&partial, model.sha256)? {
                if destination.exists() { fs::remove_file(&destination).map_err(|e| format!("Could not replace existing model: {e}"))?; }
                fs::rename(&partial, &destination).map_err(|e| format!("Could not install verified model: {e}"))?;
                let size = fs::metadata(&destination).map(|meta| meta.len()).unwrap_or(0);
                emit(events, request_id, tier, "complete", true, size, Some(size), Some("Recovered and verified completed download"));
                return Ok(());
            }
            let _ = fs::remove_file(&partial);
            offset = 0;
            download_response(&agent, &url, 0)?
        }
        Err(error) => return Err(error),
    };

    let append = offset > 0 && response.status() == 206;
    if append {
        let range = response.header("Content-Range").unwrap_or_default();
        let expected_prefix = format!("bytes {offset}-");
        if !range.starts_with(&expected_prefix) {
            let _ = fs::remove_file(&partial);
            return Err("The model server returned an invalid resume range. The partial download was removed; please retry.".into());
        }
    }
    if offset > 0 && !append {
        // Server ignored Range and returned the complete file: restart the
        // temporary file rather than appending a second copy.
        offset = 0;
    }
    if response.status() != 200 && response.status() != 206 {
        return Err(format!("Unexpected model server response: HTTP {}", response.status()));
    }

    let content_length = response.header("Content-Length").and_then(|value| value.parse::<u64>().ok());
    let total = if append { content_length.map(|length| length.saturating_add(offset)) } else { content_length };
    let mut output = if append {
        OpenOptions::new().create(true).append(true).open(&partial)
    } else {
        File::create(&partial)
    }.map_err(|e| {
        if is_storage_full(&e) {
            "Not enough disk space to download this model".to_owned()
        } else {
            format!("Could not create temporary model file: {e}")
        }
    })?;

    emit(events, request_id, tier, "downloading", true, offset, total, None);
    let mut reader = response.into_reader();
    let mut buffer = vec![0u8; CHUNK_BYTES];
    let mut received = offset;
    let mut last_emit = Instant::now();
    loop {
        if cancel.load(Ordering::Acquire) {
            let _ = output.flush();
            emit(events, request_id, tier, "cancelled", false, received, total, Some("Download cancelled. Partial data was kept for resuming."));
            return Ok(());
        }
        let count = reader.read(&mut buffer).map_err(|e| format!("Model download interrupted: {e}"))?;
        if count == 0 { break; }
        output.write_all(&buffer[..count]).map_err(|e| {
            if is_storage_full(&e) {
                "Not enough disk space to finish downloading this model".to_owned()
            } else {
                format!("Could not write model data: {e}")
            }
        })?;
        received = received.saturating_add(count as u64);
        if last_emit.elapsed().as_millis() >= 250 {
            emit(events, request_id, tier, "downloading", true, received, total, None);
            last_emit = Instant::now();
        }
    }
    output.flush().map_err(|e| format!("Could not flush model file: {e}"))?;
    output.sync_all().map_err(|e| format!("Could not finalize model download: {e}"))?;
    drop(output);

    if cancel.load(Ordering::Acquire) {
        emit(events, request_id, tier, "cancelled", false, received, total, Some("Download cancelled. Partial data was kept for resuming."));
        return Ok(());
    }

    emit(events, request_id, tier, "verifying", true, received, total, Some("Checking model integrity"));
    let actual_hash = sha256_file(&partial)?;
    if cancel.load(Ordering::Acquire) {
        emit(events, request_id, tier, "cancelled", false, received, total, Some("Download cancelled during verification. The partial file was kept."));
        return Ok(());
    }
    if !actual_hash.eq_ignore_ascii_case(model.sha256) {
        let _ = fs::remove_file(&partial);
        return Err(format!("Model integrity check failed for {}. The incomplete file was removed; please retry.", model.label));
    }

    if destination.exists() {
        fs::remove_file(&destination).map_err(|e| format!("Could not replace existing model: {e}"))?;
    }
    fs::rename(&partial, &destination).map_err(|e| format!("Could not install verified model: {e}"))?;
    emit(events, request_id, tier, "complete", true, received, total, Some("Model downloaded and verified"));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn catalog_has_three_unique_downloadable_tiers() {
        assert_eq!(MODELS.len(), 3);
        assert_eq!(model_for_tier("performance").unwrap().file_name, "ggml-base-q8_0.bin");
        assert_eq!(model_for_tier("balanced").unwrap().file_name, "ggml-small-q8_0.bin");
        assert_eq!(model_for_tier("quality").unwrap().file_name, "ggml-medium-q5_0.bin");
        assert!(model_for_tier("unknown").is_none());
    }

    #[test]
    fn all_pinned_sha256_values_are_well_formed() {
        for model in MODELS {
            assert_eq!(model.sha256.len(), 64);
            assert!(model.sha256.bytes().all(|byte| byte.is_ascii_hexdigit()));
        }
    }
}

pub fn model_worker(
    tasks: std::sync::mpsc::Receiver<(u64, String, Arc<AtomicBool>)>,
    events: Sender<String>,
    cancellations: Arc<std::sync::Mutex<std::collections::HashMap<u64, Arc<AtomicBool>>>>,
) {
    while let Ok((request_id, tier, cancel)) = tasks.recv() {
        let result = download_model(request_id, &tier, Arc::clone(&cancel), &events);
        if let Err(error) = result {
            emit(&events, request_id, &tier, "error", false, 0, None, Some(&error));
        }
        if let Ok(mut active) = cancellations.lock() {
            active.remove(&request_id);
        }
    }
}
