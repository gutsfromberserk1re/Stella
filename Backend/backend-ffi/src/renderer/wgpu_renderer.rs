use std::collections::HashMap;
use std::sync::{Arc, Mutex, OnceLock};

use wgpu::util::DeviceExt;

use super::params::LayerUniforms;

const SHADER: &str = include_str!("preview.wgsl");

struct SourceResources {
    width: u32,
    height: u32,
    // Retain the texture alongside its view for the lifetime of any bind group.
    _texture: wgpu::Texture,
    view: wgpu::TextureView,
}

struct LayerResources {
    width: u32,
    height: u32,
    source_key: String,
    uniform_buffer: wgpu::Buffer,
    bind_group: wgpu::BindGroup,
}

struct TargetResources {
    width: u32,
    height: u32,
    padded_row: u32,
    _texture: wgpu::Texture,
    view: wgpu::TextureView,
    readback: wgpu::Buffer,
}

struct RendererState {
    device: Arc<wgpu::Device>,
    queue: Arc<wgpu::Queue>,
    pipeline: wgpu::RenderPipeline,
    sampler: wgpu::Sampler,
    // Source pixels are cached independently from clip/layer uniforms. Multiple
    // layers can therefore share one texture upload while retaining separate
    // transforms, opacity, keyframes and blend order.
    sources: HashMap<String, SourceResources>,
    layers: HashMap<String, LayerResources>,
    target: Option<TargetResources>,
}

static GPU: OnceLock<Mutex<RendererState>> = OnceLock::new();

fn state() -> Result<&'static Mutex<RendererState>, String> {
    if let Some(state) = GPU.get() { return Ok(state); }
    let created = pollster::block_on(async {
        let instance = wgpu::Instance::default();
        let adapter = instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: None,
            force_fallback_adapter: false,
        }).await.ok_or_else(|| "wgpu could not find a usable GPU adapter".to_string())?;
        let (device, queue) = adapter.request_device(&wgpu::DeviceDescriptor {
            label: Some("Stella preview device"), required_features: wgpu::Features::empty(), required_limits: adapter.limits(),
        }, None).await.map_err(|e| format!("wgpu device creation failed: {e}"))?;
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Stella real-time video compositor shader"), source: wgpu::ShaderSource::Wgsl(SHADER.into()),
        });
        let bind_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("Stella preview bind group layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture { sample_type: wgpu::TextureSampleType::Float { filterable: true }, view_dimension: wgpu::TextureViewDimension::D2, multisampled: false }, count: None },
                wgpu::BindGroupLayoutEntry { binding: 1, visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering), count: None },
                wgpu::BindGroupLayoutEntry { binding: 2, visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None },
            ],
        });
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("Stella preview pipeline layout"), bind_group_layouts: &[&bind_layout], push_constant_ranges: &[],
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Stella GPU preview compositor"), layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState { module: &shader, entry_point: "vs_main", buffers: &[] },
            fragment: Some(wgpu::FragmentState { module: &shader, entry_point: "fs_main", targets: &[Some(wgpu::ColorTargetState {
                format: wgpu::TextureFormat::Rgba8Unorm,
                blend: Some(wgpu::BlendState::ALPHA_BLENDING), write_mask: wgpu::ColorWrites::ALL,
            })] }),
            primitive: wgpu::PrimitiveState::default(), depth_stencil: None, multisample: wgpu::MultisampleState::default(), multiview: None,
        });
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("Stella preview linear sampler"), address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge, address_mode_w: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Linear, min_filter: wgpu::FilterMode::Linear,
            mipmap_filter: wgpu::FilterMode::Nearest, ..Default::default()
        });
        Ok::<_, String>(RendererState { device: Arc::new(device), queue: Arc::new(queue), pipeline, sampler,
            sources: HashMap::new(), layers: HashMap::new(), target: None })
    })?;
    let _ = GPU.set(Mutex::new(created));
    GPU.get().ok_or_else(|| "wgpu renderer initialization failed".to_string())
}

pub struct GpuLayerFrame<'a> {
    pub key: &'a str,
    /// Identity of the decoded source frame, shared by duplicate clip layers.
    pub source_key: &'a str,
    pub pixels: &'a [u8],
    pub width: u32,
    pub height: u32,
    pub uniforms: LayerUniforms,
}

pub struct GpuPreviewRenderer;
impl GpuPreviewRenderer {
    pub fn composite_rgba(layers: &[GpuLayerFrame<'_>], tw: u32, th: u32) -> Result<Vec<u8>, String> {
        if tw == 0 || th == 0 { return Err("GPU preview dimensions must be nonzero".into()); }
        for layer in layers {
            if layer.width == 0 || layer.height == 0 { return Err("GPU layer dimensions must be nonzero".into()); }
            let expected = layer.width as usize * layer.height as usize * 4;
            if layer.pixels.len() != expected { return Err(format!("Decoded layer is {} bytes; expected {expected}", layer.pixels.len())); }
        }
        let mut renderer = state()?.lock().map_err(|_| "wgpu renderer lock failed")?;
        // Clone the Arc handles so resource creation does not keep an immutable
        // borrow of `renderer` alive while the layer/target caches are mutated.
        let device = Arc::clone(&renderer.device);
        let queue = Arc::clone(&renderer.queue);

        if renderer.target.as_ref().is_none_or(|target| target.width != tw || target.height != th) {
            let texture = device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Stella persistent preview target"), size: wgpu::Extent3d { width: tw, height: th, depth_or_array_layers: 1 },
                mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Rgba8Unorm,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC, view_formats: &[],
            });
            let view = texture.create_view(&Default::default());
            let padded_row = (tw * 4 + wgpu::COPY_BYTES_PER_ROW_ALIGNMENT - 1) & !(wgpu::COPY_BYTES_PER_ROW_ALIGNMENT - 1);
            let readback = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("Stella persistent preview readback"), size: padded_row as u64 * th as u64,
                usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ, mapped_at_creation: false,
            });
            renderer.target = Some(TargetResources { width: tw, height: th, padded_row, _texture: texture, view, readback });
        }

        for layer in layers {
            let uniforms = layer.uniforms.with_sizes(layer.width, layer.height, tw, th);

            // A source-frame texture is uploaded once, regardless of how many
            // timeline clips reference it. New frames get new bounded cache
            // entries; an existing key is already populated with the same RGBA.
            if !renderer.sources.contains_key(layer.source_key) {
                let texture = device.create_texture(&wgpu::TextureDescriptor {
                    label: Some("Stella shared preview source texture"),
                    size: wgpu::Extent3d { width: layer.width, height: layer.height, depth_or_array_layers: 1 },
                    mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
                    format: wgpu::TextureFormat::Rgba8Unorm,
                    usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                    view_formats: &[],
                });
                queue.write_texture(
                    wgpu::ImageCopyTexture { texture: &texture, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
                    layer.pixels,
                    wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(layer.width * 4), rows_per_image: Some(layer.height) },
                    wgpu::Extent3d { width: layer.width, height: layer.height, depth_or_array_layers: 1 },
                );
                let view = texture.create_view(&Default::default());
                renderer.sources.insert(layer.source_key.to_owned(), SourceResources {
                    width: layer.width, height: layer.height, _texture: texture, view,
                });
            }

            let recreate_layer = renderer.layers.get(layer.key).is_none_or(|resource|
                resource.width != layer.width || resource.height != layer.height || resource.source_key != layer.source_key);
            if recreate_layer {
                // Preserve the layer's uniform buffer when only its source frame
                // changes; rebuild only the bind group to point at the shared
                // source texture for this frame.
                let uniform_buffer = renderer.layers.remove(layer.key)
                    .map(|resource| resource.uniform_buffer)
                    .unwrap_or_else(|| device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                        label: Some("Stella persistent layer uniforms"),
                        contents: bytemuck::bytes_of(&uniforms),
                        usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                    }));
                let source = renderer.sources.get(layer.source_key).expect("shared source texture was inserted");
                debug_assert_eq!(source.width, layer.width);
                debug_assert_eq!(source.height, layer.height);
                let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
                    label: Some("Stella persistent layer bind group"), layout: &renderer.pipeline.get_bind_group_layout(0),
                    entries: &[
                        wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(&source.view) },
                        wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::Sampler(&renderer.sampler) },
                        wgpu::BindGroupEntry { binding: 2, resource: uniform_buffer.as_entire_binding() },
                    ],
                });
                renderer.layers.insert(layer.key.to_owned(), LayerResources {
                    width: layer.width, height: layer.height, source_key: layer.source_key.to_owned(),
                    uniform_buffer, bind_group,
                });
            }
            if let Some(resource) = renderer.layers.get(layer.key) {
                queue.write_buffer(&resource.uniform_buffer, 0, bytemuck::bytes_of(&uniforms));
            }
        }

        // Keep only a modest working set of source textures in memory. Never
        // evict a frame used by this composite; prune matching stale layer
        // bindings too, so a later re-use cannot accidentally sample an old view.
        {
            let active_sources: std::collections::HashSet<&str> = layers.iter().map(|layer| layer.source_key).collect();
            // Retain only four previous preview frames beyond the currently
            // active sources; a fixed 24-frame cache could consume hundreds of
            // MiB at 1080p, especially with many simultaneous video layers.
            let keep_sources = active_sources.len() + 4;
            let stale: Vec<String> = renderer.sources.keys()
                .filter(|key| !active_sources.contains(key.as_str()))
                .take(renderer.sources.len().saturating_sub(keep_sources)).cloned().collect();
            for key in stale {
                renderer.sources.remove(&key);
                renderer.layers.retain(|_, layer| layer.source_key != key);
            }
        }
        {
            let active_layers: std::collections::HashSet<&str> = layers.iter().map(|layer| layer.key).collect();
            let keep_layers = active_layers.len() + 8;
            let stale: Vec<String> = renderer.layers.keys()
                .filter(|key| !active_layers.contains(key.as_str()))
                .take(renderer.layers.len().saturating_sub(keep_layers)).cloned().collect();
            for key in stale { renderer.layers.remove(&key); }
        }

        let target = renderer.target.as_ref().expect("persistent target initialized");
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: Some("Stella persistent render encoder") });
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Stella GPU composite pass"), color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target.view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK), store: wgpu::StoreOp::Store },
                })], depth_stencil_attachment: None, occlusion_query_set: None, timestamp_writes: None,
            });
            pass.set_pipeline(&renderer.pipeline);
            for layer in layers {
                if let Some(resource) = renderer.layers.get(layer.key) {
                    pass.set_bind_group(0, &resource.bind_group, &[]);
                    pass.draw(0..3, 0..1);
                }
            }
        }
        let target = renderer.target.as_ref().expect("persistent target initialized");
        encoder.copy_texture_to_buffer(wgpu::ImageCopyTexture { texture: &target._texture, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
            wgpu::ImageCopyBuffer { buffer: &target.readback, layout: wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(target.padded_row), rows_per_image: Some(th) } },
            wgpu::Extent3d { width: tw, height: th, depth_or_array_layers: 1 });
        queue.submit(Some(encoder.finish()));

        // Readback is still required by the current Raylib/OpenGL preview surface.
        // It runs on the worker thread, never the UI thread; target and staging buffer persist.
        let slice = target.readback.slice(..);
        let (tx, rx) = std::sync::mpsc::channel();
        slice.map_async(wgpu::MapMode::Read, move |result| { let _ = tx.send(result); });
        device.poll(wgpu::Maintain::Wait);
        rx.recv().map_err(|e| format!("GPU readback callback failed: {e}"))?.map_err(|e| format!("GPU readback mapping failed: {e}"))?;
        let mapped = slice.get_mapped_range();
        let mut output = vec![0u8; tw as usize * th as usize * 4];
        for y in 0..th as usize {
            let src = &mapped[y * target.padded_row as usize..y * target.padded_row as usize + tw as usize * 4];
            let dst = &mut output[y * tw as usize * 4..(y + 1) * tw as usize * 4];
            dst.copy_from_slice(src);
        }
        drop(mapped);
        target.readback.unmap();
        Ok(output)
    }
}
