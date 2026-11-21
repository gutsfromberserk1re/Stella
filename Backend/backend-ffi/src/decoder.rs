//! Persistent in-process FFmpeg video decoder used by the preview worker.
//!
//! A decoder context is opened once per media source and reused for sequential
//! playback. Seeking flushes that context instead of spawning ffmpeg/ffprobe
//! processes for every preview frame. Recently decoded frames are kept in a
//! bounded LRU-style cache so scrubbing back to a nearby frame is immediate.

use std::{
    collections::{HashMap, VecDeque},
    path::Path,
    sync::{Arc, OnceLock},
};

use ffmpeg_next as ffmpeg;
use ffmpeg::{
    codec,
    format,
    frame,
    media::Type,
    util::{
        format::Pixel,
        rational::Rational,
    },
};

const MAX_CACHED_FRAME_BYTES: usize = 128 * 1024 * 1024;
const MAX_OPEN_DECODERS: usize = 8;
const SEEK_REUSE_WINDOW_SECONDS: f64 = 1.25;

static FFMPEG_INIT: OnceLock<Result<(), String>> = OnceLock::new();

fn ensure_ffmpeg() -> Result<(), String> {
    match FFMPEG_INIT.get_or_init(|| ffmpeg::init().map_err(|e| e.to_string())) {
        Ok(()) => Ok(()),
        Err(error) => Err(format!("FFmpeg decoder initialization failed: {error}")),
    }
}

#[derive(Clone)]
pub struct DecodedFrame {
    pub width: u32,
    pub height: u32,
    pub pts_seconds: f64,
    pub rgba: Arc<Vec<u8>>,
}

#[derive(Clone, Hash, Eq, PartialEq)]
struct FrameKey {
    path: String,
    index: i64,
}

struct VideoDecoder {
    input: format::context::Input,
    decoder: ffmpeg::decoder::Video,
    stream_index: usize,
    time_base: Rational,
    frame_rate: f64,
    cursor_seconds: f64,
}

impl VideoDecoder {
    fn open(path: &str) -> Result<Self, String> {
        ensure_ffmpeg()?;
        if !Path::new(path).is_file() {
            return Err(format!("Media file is missing: {path}"));
        }

        let input = format::input(path)
            .map_err(|error| format!("Could not open video source '{path}': {error}"))?;
        let (stream_index, time_base, frame_rate, decoder) = {
            let stream = input
                .streams()
                .best(Type::Video)
                .ok_or_else(|| format!("No video stream found in '{path}'"))?;
            let stream_index = stream.index();
            let time_base = stream.time_base();
            let rate = stream.avg_frame_rate();
            let fps = f64::from(rate);
            let fps = if fps.is_finite() && fps > 0.1 && fps < 1000.0 { fps } else { 30.0 };
            let context = codec::context::Context::from_parameters(stream.parameters())
                .map_err(|error| format!("Could not read video stream parameters: {error}"))?;
            let decoder = context.decoder().video()
                .map_err(|error| format!("Could not initialize video decoder: {error}"))?;
            (stream_index, time_base, fps, decoder)
        };
        Ok(Self {
            input,
            decoder,
            stream_index,
            time_base,
            frame_rate,
            cursor_seconds: -1.0,
        })
    }

    fn seek(&mut self, seconds: f64) -> Result<(), String> {
        let timestamp = (seconds.max(0.0) * 1_000_000.0).round() as i64;
        self.input.seek(timestamp, ..timestamp)
            .map_err(|error| format!("Video seek failed: {error}"))?;
        self.input.clear_eof();
        self.decoder.flush();
        self.cursor_seconds = -1.0;
        Ok(())
    }

    fn decode_first_frame(&mut self) -> Result<DecodedFrame, String> {
        let mut decoded = frame::Video::empty();
        let mut packet = ffmpeg::Packet::empty();
        loop {
            match packet.read(&mut self.input) {
                Ok(()) => {
                    if packet.stream() == self.stream_index {
                        self.decoder.send_packet(&packet)
                            .map_err(|error| format!("Thumbnail decode packet failed: {error}"))?;
                        while self.decoder.receive_frame(&mut decoded).is_ok() {
                            let timestamp = decoded.timestamp().or_else(|| decoded.pts())
                                .map(|pts| pts as f64 * f64::from(self.time_base)).unwrap_or(0.0);
                            return convert_frame_to_rgba(&decoded, timestamp);
                        }
                    }
                }
                Err(ffmpeg::Error::Eof) => {
                    self.decoder.send_eof()
                        .map_err(|error| format!("Thumbnail decoder flush failed: {error}"))?;
                    while self.decoder.receive_frame(&mut decoded).is_ok() {
                        let timestamp = decoded.timestamp().or_else(|| decoded.pts())
                            .map(|pts| pts as f64 * f64::from(self.time_base)).unwrap_or(0.0);
                        return convert_frame_to_rgba(&decoded, timestamp);
                    }
                    return Err("No video frame was available for this media thumbnail".into());
                }
                Err(error) => return Err(format!("Could not read media for thumbnail: {error}")),
            }
        }
    }

    fn decode_at(&mut self, target_seconds: f64) -> Result<DecodedFrame, String> {
        let target = target_seconds.max(0.0);
        let tolerance = (0.5 / self.frame_rate).max(0.002);
        let must_seek = self.cursor_seconds < 0.0
            || target + tolerance < self.cursor_seconds
            || target - self.cursor_seconds > SEEK_REUSE_WINDOW_SECONDS;
        if must_seek { self.seek(target)?; }

        let mut decoded = frame::Video::empty();
        let mut packet = ffmpeg::Packet::empty();
        loop {
            // Drain any frames already queued by the previous packet before
            // reading another packet. This keeps decoder state continuous.
            while self.decoder.receive_frame(&mut decoded).is_ok() {
                let timestamp = decoded.timestamp().or_else(|| decoded.pts());
                let pts_seconds = timestamp
                    .map(|pts| pts as f64 * f64::from(self.time_base))
                    .unwrap_or(target);
                self.cursor_seconds = pts_seconds;
                if pts_seconds + tolerance >= target {
                    return convert_frame_to_rgba(&decoded, pts_seconds);
                }
            }

            match packet.read(&mut self.input) {
                Ok(()) => {
                    if packet.stream() == self.stream_index {
                        self.decoder.send_packet(&packet)
                            .map_err(|error| format!("Video packet decode failed: {error}"))?;
                    }
                }
                Err(ffmpeg::Error::Eof) => {
                    self.decoder.send_eof().map_err(|error| format!("Video decoder flush failed: {error}"))?;
                    while self.decoder.receive_frame(&mut decoded).is_ok() {
                        let timestamp = decoded.timestamp().or_else(|| decoded.pts());
                        let pts_seconds = timestamp
                            .map(|pts| pts as f64 * f64::from(self.time_base))
                            .unwrap_or(target);
                        self.cursor_seconds = pts_seconds;
                        if pts_seconds + tolerance >= target {
                            return convert_frame_to_rgba(&decoded, pts_seconds);
                        }
                    }
                    return Err(format!("No decoded video frame at {target:.3}s"));
                }
                Err(error) => return Err(format!("Could not read video packet: {error}")),
            }
        }
    }

}

/// Decode a single source frame in-process, downscale it to a small 16:9 bitmap,
/// and write a BMP that Raylib can load in both the lite and full distributions.
/// This deliberately avoids the FFmpeg CLI so media-library thumbnails work in
/// the lite package (which ships decoder libraries but not ffmpeg.exe).
pub fn write_thumbnail(path: &str, destination: &Path, target_width: u32, target_height: u32) -> Result<(), String> {
    if target_width == 0 || target_height == 0 {
        return Err("Thumbnail dimensions must be nonzero".into());
    }
    let mut decoder = VideoDecoder::open(path)?;
    let decoded = decoder.decode_first_frame()?;
    let thumbnail = resize_rgba_fit(&decoded, target_width, target_height)?;
    write_bmp24(destination, target_width, target_height, &thumbnail)
}

fn resize_rgba_fit(source: &DecodedFrame, target_width: u32, target_height: u32) -> Result<Vec<u8>, String> {
    let source_width = source.width as usize;
    let source_height = source.height as usize;
    let expected = source_width.checked_mul(source_height).and_then(|n| n.checked_mul(4))
        .ok_or_else(|| "Thumbnail source dimensions overflowed".to_owned())?;
    if source.rgba.len() != expected || source_width == 0 || source_height == 0 {
        return Err("Thumbnail source frame has invalid RGBA data".into());
    }
    let target_w = target_width as usize;
    let target_h = target_height as usize;
    let scale = (target_w as f64 / source_width as f64).min(target_h as f64 / source_height as f64);
    let draw_w = ((source_width as f64 * scale).round() as usize).clamp(1, target_w);
    let draw_h = ((source_height as f64 * scale).round() as usize).clamp(1, target_h);
    let offset_x = (target_w - draw_w) / 2;
    let offset_y = (target_h - draw_h) / 2;
    let mut output = vec![0u8; target_w * target_h * 4];
    // Opaque dark backing makes transparent PNGs and letterboxed media thumbnails clean.
    for pixel in output.chunks_exact_mut(4) { pixel.copy_from_slice(&[12, 13, 17, 255]); }

    let sample = |x: usize, y: usize, channel: usize| -> f64 {
        source.rgba[(y * source_width + x) * 4 + channel] as f64
    };
    for dy in 0..draw_h {
        let sy = (((dy as f64 + 0.5) * source_height as f64 / draw_h as f64) - 0.5)
            .clamp(0.0, source_height.saturating_sub(1) as f64);
        let y0 = sy.floor() as usize;
        let y1 = (y0 + 1).min(source_height - 1);
        let fy = sy - y0 as f64;
        for dx in 0..draw_w {
            let sx = (((dx as f64 + 0.5) * source_width as f64 / draw_w as f64) - 0.5)
                .clamp(0.0, source_width.saturating_sub(1) as f64);
            let x0 = sx.floor() as usize;
            let x1 = (x0 + 1).min(source_width - 1);
            let fx = sx - x0 as f64;
            let destination_index = ((offset_y + dy) * target_w + offset_x + dx) * 4;
            let alpha = (sample(x0, y0, 3) * (1.0 - fx) * (1.0 - fy)
                + sample(x1, y0, 3) * fx * (1.0 - fy)
                + sample(x0, y1, 3) * (1.0 - fx) * fy
                + sample(x1, y1, 3) * fx * fy) / 255.0;
            for channel in 0..3 {
                let value = sample(x0, y0, channel) * (1.0 - fx) * (1.0 - fy)
                    + sample(x1, y0, channel) * fx * (1.0 - fy)
                    + sample(x0, y1, channel) * (1.0 - fx) * fy
                    + sample(x1, y1, channel) * fx * fy;
                // Composite alpha over the dark library-tile background.
                output[destination_index + channel] = (value * alpha / 255.0
                    + [12.0, 13.0, 17.0][channel] * (1.0 - alpha)).round().clamp(0.0, 255.0) as u8;
            }
            output[destination_index + 3] = 255;
        }
    }
    Ok(output)
}

fn write_bmp24(destination: &Path, width: u32, height: u32, rgba: &[u8]) -> Result<(), String> {
    let row_bytes = width.checked_mul(3).ok_or_else(|| "Thumbnail bitmap row size overflowed".to_owned())?;
    let padded_row = (row_bytes + 3) & !3;
    let image_bytes = padded_row.checked_mul(height).ok_or_else(|| "Thumbnail bitmap size overflowed".to_owned())?;
    let file_bytes = 54u32.checked_add(image_bytes).ok_or_else(|| "Thumbnail bitmap file size overflowed".to_owned())?;
    if rgba.len() != width as usize * height as usize * 4 {
        return Err("Thumbnail bitmap received invalid RGBA data".into());
    }

    let mut header = [0u8; 54];
    header[0..2].copy_from_slice(b"BM");
    header[2..6].copy_from_slice(&file_bytes.to_le_bytes());
    header[10..14].copy_from_slice(&54u32.to_le_bytes());
    header[14..18].copy_from_slice(&40u32.to_le_bytes());
    header[18..22].copy_from_slice(&(width as i32).to_le_bytes());
    header[22..26].copy_from_slice(&(height as i32).to_le_bytes());
    header[26..28].copy_from_slice(&1u16.to_le_bytes());
    header[28..30].copy_from_slice(&24u16.to_le_bytes());
    header[34..38].copy_from_slice(&image_bytes.to_le_bytes());
    header[38..42].copy_from_slice(&2835i32.to_le_bytes());
    header[42..46].copy_from_slice(&2835i32.to_le_bytes());

    let mut file = std::fs::File::create(destination)
        .map_err(|error| format!("Could not create media thumbnail '{}': {error}", destination.display()))?;
    use std::io::Write;
    file.write_all(&header).map_err(|error| format!("Could not write thumbnail header: {error}"))?;
    let mut row = vec![0u8; padded_row as usize];
    for y in (0..height as usize).rev() {
        let row_start = y * width as usize * 4;
        for x in 0..width as usize {
            let source_index = row_start + x * 4;
            let target_index = x * 3;
            row[target_index..target_index + 3]
                .copy_from_slice(&[rgba[source_index + 2], rgba[source_index + 1], rgba[source_index]]);
        }
        file.write_all(&row).map_err(|error| format!("Could not write thumbnail row: {error}"))?;
    }
    file.flush().map_err(|error| format!("Could not finish media thumbnail: {error}"))
}

fn clamp_byte(value: f32) -> u8 { value.round().clamp(0.0, 255.0) as u8 }

fn yuv_to_rgb(y: u8, u: u8, v: u8, full_range: bool) -> [u8; 3] {
    let yy = y as f32;
    let uu = u as f32 - 128.0;
    let vv = v as f32 - 128.0;
    if full_range {
        [
            clamp_byte(yy + 1.402 * vv),
            clamp_byte(yy - 0.344_136 * uu - 0.714_136 * vv),
            clamp_byte(yy + 1.772 * uu),
        ]
    } else {
        let c = (yy - 16.0).max(0.0);
        [
            clamp_byte((1.164_383 * c) + 1.596_027 * vv),
            clamp_byte((1.164_383 * c) - 0.391_762 * uu - 0.812_968 * vv),
            clamp_byte((1.164_383 * c) + 2.017_232 * uu),
        ]
    }
}


#[inline]
fn read_u16_le(data: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([data[offset], data[offset + 1]])
}

#[inline]
fn yuv_to_rgb_depth(y: u16, u: u16, v: u16, depth: u8) -> [u8; 3] {
    let scale = (1u32 << (depth - 8)) as f32;
    let yy = ((y as f32) - 16.0 * scale) / (219.0 * scale);
    let uu = ((u as f32) - 128.0 * scale) / (224.0 * scale);
    let vv = ((v as f32) - 128.0 * scale) / (224.0 * scale);
    [
        clamp_byte((yy + 1.402 * vv) * 255.0),
        clamp_byte((yy - 0.344_136 * uu - 0.714_136 * vv) * 255.0),
        clamp_byte((yy + 1.772 * uu) * 255.0),
    ]
}

fn convert_planar_yuv16(
    source: &frame::Video,
    width: u32,
    height: u32,
    rgba: &mut [u8],
    depth: u8,
    chroma_x_shift: usize,
    chroma_y_shift: usize,
) {
    let y_plane = source.data(0);
    let u_plane = source.data(1);
    let v_plane = source.data(2);
    let y_stride = source.stride(0);
    let u_stride = source.stride(1);
    let v_stride = source.stride(2);
    for y in 0..height as usize {
        for x in 0..width as usize {
            let yy = read_u16_le(y_plane, y * y_stride + x * 2);
            let uu = read_u16_le(u_plane, (y >> chroma_y_shift) * u_stride + (x >> chroma_x_shift) * 2);
            let vv = read_u16_le(v_plane, (y >> chroma_y_shift) * v_stride + (x >> chroma_x_shift) * 2);
            let rgb = yuv_to_rgb_depth(yy, uu, vv, depth);
            let d = (y * width as usize + x) * 4;
            rgba[d..d + 4].copy_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
        }
    }
}

fn convert_frame_to_rgba(source: &frame::Video, pts_seconds: f64) -> Result<DecodedFrame, String> {
    let width = source.width();
    let height = source.height();
    if width == 0 || height == 0 { return Err("Decoder returned an empty video frame".into()); }
    let mut rgba = vec![0u8; width as usize * height as usize * 4];
    let pixel_format = source.format();

    match pixel_format {
        Pixel::RGBA | Pixel::BGRA | Pixel::ARGB | Pixel::ABGR => {
            let src = source.data(0);
            let stride = source.stride(0);
            let bpp = 4usize;
            for y in 0..height as usize {
                let row = &src[y * stride..y * stride + width as usize * bpp];
                for x in 0..width as usize {
                    let s = x * 4;
                    let d = (y * width as usize + x) * 4;
                    let (r, g, b, a) = match pixel_format {
                        Pixel::RGBA => (row[s], row[s + 1], row[s + 2], row[s + 3]),
                        Pixel::BGRA => (row[s + 2], row[s + 1], row[s], row[s + 3]),
                        Pixel::ARGB => (row[s + 1], row[s + 2], row[s + 3], row[s]),
                        Pixel::ABGR => (row[s + 3], row[s + 2], row[s + 1], row[s]),
                        _ => unreachable!(),
                    };
                    rgba[d..d + 4].copy_from_slice(&[r, g, b, a]);
                }
            }
        }
        Pixel::RGB24 | Pixel::BGR24 => {
            let src = source.data(0);
            let stride = source.stride(0);
            for y in 0..height as usize {
                let row = &src[y * stride..y * stride + width as usize * 3];
                for x in 0..width as usize {
                    let s = x * 3;
                    let d = (y * width as usize + x) * 4;
                    let (r, g, b) = if pixel_format == Pixel::RGB24 {
                        (row[s], row[s + 1], row[s + 2])
                    } else { (row[s + 2], row[s + 1], row[s]) };
                    rgba[d..d + 4].copy_from_slice(&[r, g, b, 255]);
                }
            }
        }
        Pixel::YUV420P | Pixel::YUVJ420P | Pixel::YUV422P | Pixel::YUVJ422P | Pixel::YUV444P | Pixel::YUVJ444P => {
            let y_plane = source.data(0); let u_plane = source.data(1); let v_plane = source.data(2);
            let ys = source.stride(0); let us = source.stride(1); let vs = source.stride(2);
            let (sx, sy, full) = match pixel_format {
                Pixel::YUV420P => (1usize, 1usize, false), Pixel::YUVJ420P => (1, 1, true),
                Pixel::YUV422P => (1, 0, false), Pixel::YUVJ422P => (1, 0, true),
                Pixel::YUV444P => (0, 0, false), Pixel::YUVJ444P => (0, 0, true),
                _ => unreachable!(),
            };
            for y in 0..height as usize {
                for x in 0..width as usize {
                    let yy = y_plane[y * ys + x];
                    let uu = u_plane[(y >> sy) * us + (x >> sx)];
                    let vv = v_plane[(y >> sy) * vs + (x >> sx)];
                    let rgb = yuv_to_rgb(yy, uu, vv, full);
                    let d = (y * width as usize + x) * 4;
                    rgba[d..d + 4].copy_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
                }
            }
        }
        Pixel::NV12 | Pixel::NV21 => {
            let y_plane = source.data(0); let uv_plane = source.data(1);
            let ys = source.stride(0); let uvs = source.stride(1);
            for y in 0..height as usize {
                for x in 0..width as usize {
                    let uv = (y >> 1) * uvs + (x & !1);
                    let (u, v) = if pixel_format == Pixel::NV12 {
                        (uv_plane[uv], uv_plane[uv + 1])
                    } else { (uv_plane[uv + 1], uv_plane[uv]) };
                    let rgb = yuv_to_rgb(y_plane[y * ys + x], u, v, false);
                    let d = (y * width as usize + x) * 4;
                    rgba[d..d + 4].copy_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
                }
            }
        }
        Pixel::YUV420P9LE | Pixel::YUV420P10LE | Pixel::YUV420P12LE | Pixel::YUV420P16LE => {
            let depth = match pixel_format { Pixel::YUV420P9LE => 9, Pixel::YUV420P10LE => 10, Pixel::YUV420P12LE => 12, _ => 16 };
            convert_planar_yuv16(source, width, height, &mut rgba, depth, 1, 1);
        }
        Pixel::YUV422P9LE | Pixel::YUV422P10LE | Pixel::YUV422P12LE | Pixel::YUV422P16LE => {
            let depth = match pixel_format { Pixel::YUV422P9LE => 9, Pixel::YUV422P10LE => 10, Pixel::YUV422P12LE => 12, _ => 16 };
            convert_planar_yuv16(source, width, height, &mut rgba, depth, 1, 0);
        }
        Pixel::YUV444P9LE | Pixel::YUV444P10LE | Pixel::YUV444P12LE | Pixel::YUV444P16LE => {
            let depth = match pixel_format { Pixel::YUV444P9LE => 9, Pixel::YUV444P10LE => 10, Pixel::YUV444P12LE => 12, _ => 16 };
            convert_planar_yuv16(source, width, height, &mut rgba, depth, 0, 0);
        }
        Pixel::P010LE => {
            let y_plane = source.data(0); let uv_plane = source.data(1);
            let ys = source.stride(0); let uvs = source.stride(1);
            for y in 0..height as usize {
                for x in 0..width as usize {
                    let yy = read_u16_le(y_plane, y * ys + x * 2) >> 6;
                    let uv = (y >> 1) * uvs + (x >> 1) * 4;
                    let uu = read_u16_le(uv_plane, uv) >> 6;
                    let vv = read_u16_le(uv_plane, uv + 2) >> 6;
                    let rgb = yuv_to_rgb_depth(yy, uu, vv, 10);
                    let d = (y * width as usize + x) * 4;
                    rgba[d..d + 4].copy_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
                }
            }
        }
        other => return Err(format!("Preview decoder does not yet convert pixel format {other:?}. Try generating a compatible proxy (8-bit RGB/YUV or supported 9/10/12/16-bit planar YUV and P010).")),
    }

    Ok(DecodedFrame { width, height, pts_seconds, rgba: Arc::new(rgba) })
}

#[derive(Default)]
pub struct DecoderPool {
    decoders: HashMap<String, VideoDecoder>,
    decoder_order: VecDeque<String>,
    frame_rates: HashMap<String, f64>,
    frames: HashMap<FrameKey, DecodedFrame>,
    frame_order: VecDeque<FrameKey>,
    cached_bytes: usize,
}

impl DecoderPool {
    fn remember_decoder(&mut self, path: &str, decoder: VideoDecoder) {
        self.frame_rates.insert(path.to_owned(), decoder.frame_rate);
        self.decoders.insert(path.to_owned(), decoder);
        self.decoder_order.retain(|item| item != path);
        self.decoder_order.push_back(path.to_owned());
        while self.decoder_order.len() > MAX_OPEN_DECODERS {
            if let Some(oldest) = self.decoder_order.pop_front() {
                self.decoders.remove(&oldest);
            }
        }
    }

    pub fn frame_at(&mut self, path: &str, seconds: f64) -> Result<DecodedFrame, String> {
        // Check the frame cache before reopening a decoder. This matters when a
        // source was evicted from the small open-decoder pool but its pixels
        // are still hot in the larger decoded-frame cache.
        let frame_rate = if let Some(rate) = self.frame_rates.get(path).copied() {
            rate
        } else {
            let decoder = VideoDecoder::open(path)?;
            let rate = decoder.frame_rate;
            self.remember_decoder(path, decoder);
            rate
        };
        let frame_index = (seconds.max(0.0) * frame_rate).round() as i64;
        let key = FrameKey { path: path.to_owned(), index: frame_index };
        if let Some(frame) = self.frames.get(&key).cloned() {
            if let Some(index) = self.frame_order.iter().position(|item| item == &key) {
                self.frame_order.remove(index);
            }
            self.frame_order.push_back(key);
            return Ok(frame);
        }

        if !self.decoders.contains_key(path) {
            let decoder = VideoDecoder::open(path)?;
            self.remember_decoder(path, decoder);
        } else {
            self.decoder_order.retain(|item| item != path);
            self.decoder_order.push_back(path.to_owned());
        }

        let decoded = self.decoders.get_mut(path)
            .ok_or("Video decoder cache lost its source")?.decode_at(seconds)?;
        let frame_bytes = decoded.rgba.len();
        self.frames.insert(key.clone(), decoded.clone());
        self.frame_order.push_back(key);
        self.cached_bytes += frame_bytes;
        while self.cached_bytes > MAX_CACHED_FRAME_BYTES {
            let Some(oldest) = self.frame_order.pop_front() else { break; };
            if let Some(removed) = self.frames.remove(&oldest) {
                self.cached_bytes = self.cached_bytes.saturating_sub(removed.rgba.len());
            }
        }
        Ok(decoded)
    }
}

#[cfg(windows)]
#[link(name = "bcrypt")]
extern "C" {}


#[cfg(test)]
mod thumbnail_tests {
    use super::*;

    #[test]
    fn thumbnail_writer_outputs_small_loadable_bmp() {
        let source = DecodedFrame {
            width: 2,
            height: 1,
            pts_seconds: 0.0,
            rgba: Arc::new(vec![255, 0, 0, 255, 0, 255, 0, 255]),
        };
        let resized = resize_rgba_fit(&source, 4, 4).expect("thumbnail resize");
        assert_eq!(resized.len(), 4 * 4 * 4);
        assert_eq!(&resized[0..4], &[12, 13, 17, 255]); // letterbox is opaque

        let path = std::env::temp_dir().join(format!("stella-thumbnail-test-{}.bmp", std::process::id()));
        write_bmp24(&path, 4, 4, &resized).expect("write BMP thumbnail");
        let bytes = std::fs::read(&path).expect("read BMP thumbnail");
        assert_eq!(&bytes[0..2], b"BM");
        assert_eq!(u32::from_le_bytes(bytes[18..22].try_into().unwrap()), 4);
        assert_eq!(u32::from_le_bytes(bytes[22..26].try_into().unwrap()), 4);
        assert_eq!(bytes.len(), 54 + 4 * 4 * 3);
        let _ = std::fs::remove_file(path);
    }
}
