//! Offline Whisper transcription for a selected timeline clip.
//!
//! Audio is decoded in-process through the existing static FFmpeg libraries.
//! Rubato performs the only conversion FFmpeg's decode-only SDK does not ship
//! (sample-rate conversion); Whisper receives mono 16 kHz f32 samples.

use ffmpeg_next as ffmpeg;
use rubato::{Resampler, SincFixedIn, SincInterpolationParameters, SincInterpolationType, WindowFunction};
use serde_json::{json, Value};
use std::{
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc::Sender,
        Arc, Mutex,
    },
    time::{Duration, Instant},
};
use whisper_rs::{FullParams, SamplingStrategy, WhisperContext, WhisperContextParameters};

const WHISPER_RATE: u32 = 16_000;
const MAX_AUDIO_SECONDS: f64 = 30.0 * 60.0;

fn emit(
    events: &Sender<String>,
    request_id: u64,
    source_id: &str,
    state: &str,
    progress: f64,
    detail: Option<&str>,
    cues: Option<Value>,
) {
    let mut event = json!({
        "command": "transcription",
        "requestId": request_id,
        "sourceClipId": source_id,
        "state": state,
        "progress": progress.clamp(0.0, 100.0),
    });
    if let Some(detail) = detail { event["detail"] = json!(detail); }
    if let Some(cues) = cues { event["cues"] = cues; }
    let _ = events.send(event.to_string());
}

fn sample_from_bytes(bytes: &[u8], index: usize, format: ffmpeg::format::Sample) -> Result<f32, String> {
    use ffmpeg::format::Sample;
    let (offset, size) = match format {
        Sample::U8(_) => (index, 1),
        Sample::I16(_) => (index * 2, 2),
        Sample::I32(_) | Sample::F32(_) => (index * 4, 4),
        Sample::I64(_) | Sample::F64(_) => (index * 8, 8),
        Sample::None => return Err("FFmpeg returned an unknown audio sample format".into()),
    };
    if offset + size > bytes.len() { return Err("Decoded audio frame is shorter than its sample count".into()); }
    let value = match format {
        Sample::U8(_) => (bytes[offset] as f32 - 128.0) / 128.0,
        Sample::I16(_) => i16::from_ne_bytes([bytes[offset], bytes[offset + 1]]) as f32 / 32768.0,
        Sample::I32(_) => i32::from_ne_bytes(bytes[offset..offset + 4].try_into().unwrap()) as f32 / 2_147_483_648.0,
        Sample::I64(_) => (i64::from_ne_bytes(bytes[offset..offset + 8].try_into().unwrap()) as f64 / 9_223_372_036_854_775_808.0) as f32,
        Sample::F32(_) => f32::from_ne_bytes(bytes[offset..offset + 4].try_into().unwrap()),
        Sample::F64(_) => f64::from_ne_bytes(bytes[offset..offset + 8].try_into().unwrap()) as f32,
        Sample::None => return Err("Unsupported decoded audio sample format".into()),
    };
    Ok(if value.is_finite() { value.clamp(-1.0, 1.0) } else { 0.0 })
}

fn append_frame_mono(
    frame: &ffmpeg::frame::Audio,
    sample_cursor: &mut u64,
    wanted_start: u64,
    wanted_end: u64,
    output: &mut Vec<f32>,
) -> Result<(), String> {
    let rate = frame.rate();
    if rate == 0 { return Err("Audio stream reported a zero sample rate".into()); }
    let channels = frame.channels() as usize;
    if channels == 0 || channels > 32 { return Err("Audio stream has an unsupported channel count".into()); }
    let count = frame.samples();
    let format = frame.format();
    let planar = format.is_planar();
    let first = (*sample_cursor).max(wanted_start);
    let last = ((*sample_cursor).saturating_add(count as u64)).min(wanted_end);
    if last > first {
        let planes: Vec<&[u8]> = if planar {
            (0..channels).map(|channel| frame.data(channel)).collect()
        } else {
            vec![frame.data(0)]
        };
        for absolute in first..last {
            let local = (absolute - *sample_cursor) as usize;
            let mut sum = 0.0f32;
            for channel in 0..channels {
                let plane = if planar { planes[channel] } else { planes[0] };
                let sample_index = if planar { local } else { local * channels + channel };
                sum += sample_from_bytes(plane, sample_index, format)?;
            }
            output.push((sum / channels as f32).clamp(-1.0, 1.0));
        }
    }
    *sample_cursor = (*sample_cursor).saturating_add(count as u64);
    Ok(())
}

fn decode_selected_audio(
    path: &str,
    source_offset: f64,
    source_duration: f64,
    cancel: &AtomicBool,
    request_id: u64,
    source_id: &str,
    events: &Sender<String>,
) -> Result<(Vec<f32>, u32), String> {
    if !PathBuf::from(path).is_file() { return Err("The selected clip's media file is missing".into()); }
    if !source_offset.is_finite() || !source_duration.is_finite() || source_offset < 0.0 || source_duration <= 0.0 {
        return Err("The selected clip has an invalid source range".into());
    }
    if source_duration > MAX_AUDIO_SECONDS { return Err("Transcription is limited to 30 minutes per clip to keep memory use predictable".into()); }
    ffmpeg::init().map_err(|error| format!("FFmpeg audio decoder initialization failed: {error}"))?;
    let mut input = ffmpeg::format::input(path).map_err(|error| format!("Could not open selected media: {error}"))?;
    let (stream_index, mut decoder) = {
        let stream = input.streams().best(ffmpeg::media::Type::Audio)
            .ok_or_else(|| "The selected clip has no audio stream".to_owned())?;
        let index = stream.index();
        let context = ffmpeg::codec::context::Context::from_parameters(stream.parameters())
            .map_err(|error| format!("Could not configure audio decoder: {error}"))?;
        let mut decoder = context.decoder().audio()
            .map_err(|error| format!("Could not open audio decoder: {error}"))?;
        // Request float planar output to minimize conversion work. The converter
        // below also accepts the integer/packed formats some codecs insist on.
        decoder.request_format(ffmpeg::format::Sample::F32(ffmpeg::format::sample::Type::Planar));
        (index, decoder)
    };
    let mut samples = Vec::new();
    let mut sample_rate = 0u32;
    let mut cursor = 0u64;
    let mut selected_start = 0u64;
    let mut selected_end = 0u64;
    let mut decoded = ffmpeg::frame::Audio::empty();
    let mut last_progress = Instant::now();

    for (stream, packet) in input.packets() {
        if cancel.load(Ordering::Acquire) { return Err("Transcription cancelled".into()); }
        if stream.index() != stream_index { continue; }
        decoder.send_packet(&packet).map_err(|error| format!("Audio decode failed: {error}"))?;
        while decoder.receive_frame(&mut decoded).is_ok() {
            if sample_rate == 0 {
                sample_rate = decoded.rate();
                if sample_rate == 0 { return Err("Audio stream reported a zero sample rate".into()); }
                selected_start = (source_offset * sample_rate as f64).round() as u64;
                selected_end = ((source_offset + source_duration) * sample_rate as f64).round() as u64;
                let expected = selected_end.saturating_sub(selected_start).min((MAX_AUDIO_SECONDS * sample_rate as f64) as u64);
                samples.reserve(expected.min(32_000_000) as usize);
            }
            append_frame_mono(&decoded, &mut cursor, selected_start, selected_end, &mut samples)?;
            if cursor >= selected_end { break; }
        }
        if cursor >= selected_end { break; }
        if last_progress.elapsed() >= Duration::from_millis(250) {
            let decoded_seconds = cursor as f64 / sample_rate.max(1) as f64;
            let progress = if source_offset > 0.0 {
                (decoded_seconds / (source_offset + source_duration) * 18.0).min(18.0)
            } else { (decoded_seconds / source_duration * 18.0).min(18.0) };
            emit(events, request_id, source_id, "decoding", progress, Some("Decoding selected audio"), None);
            last_progress = Instant::now();
        }
    }
    decoder.send_eof().map_err(|error| format!("Audio decoder could not flush: {error}"))?;
    while decoder.receive_frame(&mut decoded).is_ok() {
        append_frame_mono(&decoded, &mut cursor, selected_start, selected_end, &mut samples)?;
    }
    if cancel.load(Ordering::Acquire) { return Err("Transcription cancelled".into()); }
    if sample_rate == 0 || samples.is_empty() {
        return Err("No audio samples were found in the selected clip range".into());
    }
    emit(events, request_id, source_id, "resampling", 20.0, Some("Preparing 16 kHz mono audio"), None);
    Ok((samples, sample_rate))
}

fn resample_16khz(input: &[f32], input_rate: u32) -> Result<Vec<f32>, String> {
    if input_rate == WHISPER_RATE { return Ok(input.to_vec()); }
    if input_rate == 0 { return Err("Cannot resample audio with a zero sample rate".into()); }
    let chunk_size = 2048usize;
    let params = SincInterpolationParameters {
        sinc_len: 128,
        f_cutoff: 0.95,
        interpolation: SincInterpolationType::Linear,
        oversampling_factor: 128,
        window: WindowFunction::BlackmanHarris2,
    };
    let mut resampler = SincFixedIn::<f32>::new(
        WHISPER_RATE as f64 / input_rate as f64,
        2.0,
        params,
        chunk_size,
        1,
    ).map_err(|error| format!("Could not initialize audio resampler: {error:?}"))?;
    let mut output = Vec::with_capacity(((input.len() as u64 * WHISPER_RATE as u64) / input_rate as u64) as usize + 4096);
    let mut offset = 0usize;
    while offset + chunk_size <= input.len() {
        let chunk = input[offset..offset + chunk_size].to_vec();
        let input_refs: [&[f32]; 1] = [&chunk];
        let converted = resampler.process(&input_refs, None)
            .map_err(|error| format!("Audio resampling failed: {error:?}"))?;
        output.extend_from_slice(&converted[0]);
        offset += chunk_size;
    }
    if offset < input.len() {
        let remaining = input.len() - offset;
        let mut padded = vec![0.0f32; chunk_size];
        padded[..remaining].copy_from_slice(&input[offset..]);
        let input_refs: [&[f32]; 1] = [&padded];
        let converted = resampler.process(&input_refs, None)
            .map_err(|error| format!("Final audio resampling failed: {error:?}"))?;
        let expected = ((remaining as f64 * WHISPER_RATE as f64 / input_rate as f64).ceil() as usize)
            .min(converted[0].len());
        output.extend_from_slice(&converted[0][..expected]);
    }
    if output.len() < 1600 { return Err("The selected audio is too short to transcribe".into()); }
    Ok(output)
}

fn transcribe(
    request_id: u64,
    request: &Value,
    cancel: Arc<AtomicBool>,
    events: &Sender<String>,
) -> Result<Value, String> {
    let tier = request["tier"].as_str().ok_or("Whisper model tier is missing")?;
    let source_id = request["sourceClipId"].as_str().ok_or("Selected source clip id is missing")?;
    let path = request["sourcePath"].as_str().ok_or("Selected media path is missing")?;
    let source_offset = request["sourceOffset"].as_f64().unwrap_or(0.0);
    let timeline_duration = request["clipDuration"].as_f64().ok_or("Selected clip duration is missing")?;
    let clip_start = request["clipStart"].as_f64().unwrap_or(0.0);
    let speed = request["speed"].as_f64().filter(|v| v.is_finite() && *v > 0.0).unwrap_or(1.0);
    let source_duration = timeline_duration * speed;
    let model_path = crate::models::model_path_for_tier(tier)?;

    emit(events, request_id, source_id, "starting", 0.0, Some("Loading Whisper model"), None);
    if cancel.load(Ordering::Acquire) { return Err("Transcription cancelled".into()); }
    let (mono, input_rate) = decode_selected_audio(path, source_offset, source_duration, &cancel, request_id, source_id, events)?;
    let audio = resample_16khz(&mono, input_rate)?;
    drop(mono); // Release the higher-rate decode buffer before loading the Whisper model.
    if cancel.load(Ordering::Acquire) { return Err("Transcription cancelled".into()); }

    emit(events, request_id, source_id, "recognizing", 22.0, Some("Running speech recognition"), None);
    let context = WhisperContext::new_with_params(&model_path, WhisperContextParameters::default())
        .map_err(|error| format!("Could not load Whisper model: {error:?}"))?;
    let mut state = context.create_state().map_err(|error| format!("Could not create Whisper state: {error:?}"))?;
    let mut params = FullParams::new(SamplingStrategy::Greedy { best_of: 1 });
    let threads = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(4).saturating_sub(2).max(1).min(8);
    params.set_n_threads(threads as i32);
    params.set_language(None);
    params.set_max_len(42);
    params.set_split_on_word(true);
    params.set_print_special(false);
    params.set_print_progress(false);
    params.set_print_realtime(false);
    params.set_print_timestamps(false);
    let progress_events = events.clone();
    let progress_source = source_id.to_owned();
    params.set_progress_callback_safe(Some(move |percent| {
        emit(&progress_events, request_id, &progress_source, "recognizing", 22.0 + percent as f64 * 0.76, Some("Transcribing speech"), None);
    }));
    let abort = Arc::clone(&cancel);
    params.set_abort_callback_safe(Some(move || abort.load(Ordering::Acquire)));
    state.full(params, &audio).map_err(|error| {
        if cancel.load(Ordering::Acquire) { "Transcription cancelled".to_owned() }
        else { format!("Whisper transcription failed: {error:?}") }
    })?;
    if cancel.load(Ordering::Acquire) { return Err("Transcription cancelled".into()); }

    let mut cues = Vec::new();
    for segment in state.as_iter() {
        let text = segment.to_string().trim().to_owned();
        if text.is_empty() { continue; }
        let start = (segment.start_timestamp().max(0) as f64 / 100.0).min(source_duration);
        let end = (segment.end_timestamp().max(0) as f64 / 100.0).min(source_duration);
        if end <= start { continue; }
        // Whisper timestamps are relative to the trimmed audio range. Keep them
        // relative in the result; the C timeline adapter maps through speed.
        cues.push(json!({ "start": start, "end": end, "text": text }));
    }
    if cues.is_empty() { return Err("Whisper did not detect any speech in the selected clip".into()); }
    Ok(json!({
        "sourceClipId": source_id,
        "clipStart": clip_start,
        "sourceOffset": source_offset,
        "speed": speed,
        "cues": cues,
    }))
}

pub fn transcription_worker(
    tasks: std::sync::mpsc::Receiver<(u64, Value, Arc<AtomicBool>)>,
    events: Sender<String>,
    cancellations: Arc<Mutex<std::collections::HashMap<u64, Arc<AtomicBool>>>>,
) {
    while let Ok((request_id, request, cancel)) = tasks.recv() {
        let source_id = request["sourceClipId"].as_str().unwrap_or_default().to_owned();
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| transcribe(request_id, &request, Arc::clone(&cancel), &events)));
        match result {
            Ok(Ok(result)) => emit(&events, request_id, &source_id, "complete", 100.0, Some("Subtitle transcription complete"), Some(result["cues"].clone())),
            Ok(Err(error)) if cancel.load(Ordering::Acquire) || error.contains("cancelled") =>
                emit(&events, request_id, &source_id, "cancelled", 0.0, Some("Transcription cancelled"), None),
            Ok(Err(error)) => emit(&events, request_id, &source_id, "error", 0.0, Some(&error), None),
            Err(_) => emit(&events, request_id, &source_id, "error", 0.0, Some("Transcription worker encountered an internal error"), None),
        }
        if let Ok(mut active) = cancellations.lock() { active.remove(&request_id); }
    }
}
