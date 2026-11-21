//! Persistent GPU source-frame texture cache for Stella's timeline renderer.
//!
//! FFmpeg remains responsible for decoding compressed media. Each decoded RGBA
//! source frame is uploaded into a reusable texture keyed by clip/source ID.
//! The cache deliberately does not read pixels back to the CPU; the next stage
//! samples these textures in the timeline compositor.

use std::collections::HashMap;
use std::sync::{Mutex, OnceLock};

static GPU: OnceLock<Result<Mutex<GpuPreview>, String>> = OnceLock::new();

struct SourceTexture {
    texture: wgpu::Texture,
    width: u32,
    height: u32,
}

struct GpuPreview {
    device: wgpu::Device,
    queue: wgpu::Queue,
    sources: HashMap<String, SourceTexture>,
}

impl GpuPreview {
    fn new() -> Result<Self, String> {
        pollster::block_on(async {
            let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::default());
            let adapter = instance.request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                compatible_surface: None,
                force_fallback_adapter: false,
            }).await.ok_or_else(|| "No compatible GPU adapter is available for Stella".to_owned())?;
            let (device, queue) = adapter.request_device(&wgpu::DeviceDescriptor {
                label: Some("Stella timeline renderer"),
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::downlevel_defaults(),
            }, None).await.map_err(|e| format!("Could not initialize Stella GPU renderer: {e}"))?;
            Ok(Self { device, queue, sources: HashMap::new() })
        })
    }

    fn upload_source_frame(&mut self, source_id: &str, width: u32, height: u32, rgba: &[u8]) -> Result<(), String> {
        if source_id.is_empty() || width == 0 || height == 0 {
            return Err("Source frame identity and dimensions are required".into());
        }
        let expected = width as usize * height as usize * 4;
        if rgba.len() != expected {
            return Err(format!("Malformed RGBA source frame: expected {expected} bytes, received {}", rgba.len()));
        }

        let needs_recreate = self.sources.get(source_id)
            .map(|entry| entry.width != width || entry.height != height)
            .unwrap_or(true);
        if needs_recreate {
            let texture = self.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Stella persistent decoded source"),
                size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Rgba8Unorm,
                usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                view_formats: &[],
            });
            self.sources.insert(source_id.to_owned(), SourceTexture { texture, width, height });
        }

        let entry = self.sources.get(source_id).ok_or("Source texture allocation failed")?;
        self.queue.write_texture(
            wgpu::ImageCopyTexture {
                texture: &entry.texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            rgba,
            wgpu::ImageDataLayout {
                offset: 0,
                bytes_per_row: Some(width * 4),
                rows_per_image: Some(height),
            },
            wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
        );
        Ok(())
    }
}

/// Uploads a decoded source frame into a persistent GPU texture.
/// Repeated calls for the same source reuse the texture allocation.
pub(crate) fn upload_source_frame(source_id: &str, width: u32, height: u32, rgba: &[u8]) -> Result<(), String> {
    let gpu = GPU.get_or_init(|| GpuPreview::new().map(Mutex::new));
    let gpu = gpu.as_ref().map_err(Clone::clone)?;
    gpu.lock().map_err(|_| "GPU renderer lock was poisoned".to_owned())?
        .upload_source_frame(source_id, width, height, rgba)
}

/// Drops cached source textures that are no longer referenced by the timeline.
pub(crate) fn retain_sources(active_source_ids: &[String]) -> Result<(), String> {
    let Some(gpu) = GPU.get() else { return Ok(()); };
    let gpu = gpu.as_ref().map_err(Clone::clone)?;
    let mut gpu = gpu.lock().map_err(|_| "GPU renderer lock was poisoned".to_owned())?;
    gpu.sources.retain(|id, _| active_source_ids.iter().any(|active| active == id));
    Ok(())
}
