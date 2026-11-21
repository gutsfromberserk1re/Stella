use std::sync::{Mutex, OnceLock};

use wgpu::util::DeviceExt;

const SHADER: &str = r#"
struct FitUniforms {
    source_size: vec2<f32>,
    target_size: vec2<f32>,
    opacity: vec4<f32>,
};
@group(0) @binding(0) var source_texture: texture_2d<f32>;
@group(0) @binding(1) var source_sampler: sampler;
@group(0) @binding(2) var<uniform> fit: FitUniforms;
struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};
@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOut {
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );
    var out: VertexOut;
    let p = positions[index];
    out.position = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return out;
}
@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    let source_aspect = fit.source_size.x / fit.source_size.y;
    let target_aspect = fit.target_size.x / fit.target_size.y;
    var fitted = vec2<f32>(1.0, 1.0);
    if (source_aspect > target_aspect) {
        fitted.y = target_aspect / source_aspect;
    } else {
        fitted.x = source_aspect / target_aspect;
    }
    let uv = (in.uv - vec2<f32>(0.5, 0.5)) / fitted + vec2<f32>(0.5, 0.5);
    if (any(uv < vec2<f32>(0.0, 0.0)) || any(uv > vec2<f32>(1.0, 1.0))) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }
    let color = textureSample(source_texture, source_sampler, uv);
    return vec4<f32>(color.rgb, color.a * fit.opacity.x);
}
"#;


#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
struct FitUniforms {
    source_size: [f32; 2],
    target_size: [f32; 2],
    opacity: [f32; 4],
}


struct RendererState {
    device: wgpu::Device,
    queue: wgpu::Queue,
    pipeline: wgpu::RenderPipeline,
    sampler: wgpu::Sampler,
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
            label: Some("Stella preview device"),
            required_features: wgpu::Features::empty(),
            required_limits: adapter.limits(),
        }, None).await.map_err(|e| format!("wgpu device creation failed: {e}"))?;
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Stella preview fit shader"),
            source: wgpu::ShaderSource::Wgsl(SHADER.into()),
        });
        // Explicit resource layout for decoded-frame sampling and fit uniforms.
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
            label: Some("Stella preview pipeline layout"),
            bind_group_layouts: &[&bind_layout],
            push_constant_ranges: &[],
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Stella GPU preview render pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState { module: &shader, entry_point: "vs_main", buffers: &[] },
            fragment: Some(wgpu::FragmentState { module: &shader, entry_point: "fs_main",
                targets: &[Some(wgpu::ColorTargetState { format: wgpu::TextureFormat::Rgba8Unorm,
                    blend: Some(wgpu::BlendState {
                        color: wgpu::BlendComponent { src_factor: wgpu::BlendFactor::SrcAlpha, dst_factor: wgpu::BlendFactor::OneMinusSrcAlpha, operation: wgpu::BlendOperation::Add },
                        alpha: wgpu::BlendComponent { src_factor: wgpu::BlendFactor::One, dst_factor: wgpu::BlendFactor::OneMinusSrcAlpha, operation: wgpu::BlendOperation::Add },
                    }), write_mask: wgpu::ColorWrites::ALL })] }),
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
        });
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("Stella preview linear sampler"),
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            address_mode_w: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            mipmap_filter: wgpu::FilterMode::Nearest,
            ..Default::default()
        });
        Ok::<_, String>(RendererState { device, queue, pipeline, sampler })
    })?;
    let _ = GPU.set(Mutex::new(created));
    GPU.get().ok_or_else(|| "wgpu renderer initialization failed".to_string())
}


pub struct GpuLayerFrame<'a> {
    pub pixels: &'a [u8],
    pub width: u32,
    pub height: u32,
    pub opacity: f32,
}


pub struct GpuPreviewRenderer;
impl GpuPreviewRenderer {
    pub fn fit_rgba(src: &[u8], sw: u32, sh: u32, tw: u32, th: u32) -> Result<Vec<u8>, String> {
        Self::composite_rgba(&[GpuLayerFrame { pixels: src, width: sw, height: sh, opacity: 1.0 }], tw, th)
    }

    /// Draw decoded frames bottom-to-top with GPU alpha blending and independent aspect-fit.
    pub fn composite_rgba(layers: &[GpuLayerFrame<'_>], tw: u32, th: u32) -> Result<Vec<u8>, String> {
        if tw == 0 || th == 0 { return Err("GPU preview dimensions must be nonzero".into()); }
        for layer in layers {
            if layer.width == 0 || layer.height == 0 { return Err("GPU layer dimensions must be nonzero".into()); }
            let expected = layer.width as usize * layer.height as usize * 4;
            if layer.pixels.len() != expected { return Err(format!("Decoded layer is {} bytes; expected {expected}", layer.pixels.len())); }
        }
        let renderer = state()?.lock().map_err(|_| "wgpu renderer lock failed")?;
        let device = &renderer.device;
        let queue = &renderer.queue;
        let target = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Stella GPU composite target"), size: wgpu::Extent3d { width: tw, height: th, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let target_view = target.create_view(&Default::default());
        let bind_layout = renderer.pipeline.get_bind_group_layout(0);
        let mut prepared = Vec::with_capacity(layers.len());
        for layer in layers {
            let source = device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Stella decoded video layer texture"), size: wgpu::Extent3d { width: layer.width, height: layer.height, depth_or_array_layers: 1 },
                mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Rgba8Unorm,
                usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                view_formats: &[],
            });
            queue.write_texture(wgpu::ImageCopyTexture { texture: &source, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
                layer.pixels, wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(layer.width * 4), rows_per_image: Some(layer.height) },
                wgpu::Extent3d { width: layer.width, height: layer.height, depth_or_array_layers: 1 });
            let source_view = source.create_view(&Default::default());
            let uniforms = FitUniforms { source_size: [layer.width as f32, layer.height as f32], target_size: [tw as f32, th as f32], opacity: [layer.opacity.clamp(0.0, 1.0), 0.0, 0.0, 0.0] };
            let uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("Stella layer uniforms"), contents: bytemuck::bytes_of(&uniforms), usage: wgpu::BufferUsages::UNIFORM,
            });
            let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Stella compositing layer bind group"), layout: &bind_layout,
                entries: &[
                    wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(&source_view) },
                    wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::Sampler(&renderer.sampler) },
                    wgpu::BindGroupEntry { binding: 2, resource: uniform_buffer.as_entire_binding() },
                ],
            });
            prepared.push((source, uniform_buffer, bind_group));
        }
        let padded_row = (tw * 4 + wgpu::COPY_BYTES_PER_ROW_ALIGNMENT - 1) & !(wgpu::COPY_BYTES_PER_ROW_ALIGNMENT - 1);
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Stella composite readback buffer"), size: padded_row as u64 * th as u64,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ, mapped_at_creation: false,
        });
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: Some("Stella composite encoder") });
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Stella multi-layer composite pass"), color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK), store: wgpu::StoreOp::Store },
                })], depth_stencil_attachment: None, occlusion_query_set: None, timestamp_writes: None,
            });
            pass.set_pipeline(&renderer.pipeline);
            for (_, _, bind_group) in &prepared { pass.set_bind_group(0, bind_group, &[]); pass.draw(0..3, 0..1); }
        }
        encoder.copy_texture_to_buffer(wgpu::ImageCopyTexture { texture: &target, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
            wgpu::ImageCopyBuffer { buffer: &readback, layout: wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(padded_row), rows_per_image: Some(th) } },
            wgpu::Extent3d { width: tw, height: th, depth_or_array_layers: 1 });
        queue.submit(Some(encoder.finish()));
        let slice = readback.slice(..);
        let (tx, rx) = std::sync::mpsc::channel();
        slice.map_async(wgpu::MapMode::Read, move |result| { let _ = tx.send(result); });
        device.poll(wgpu::Maintain::Wait);
        rx.recv().map_err(|e| format!("GPU readback callback failed: {e}"))?.map_err(|e| format!("GPU readback mapping failed: {e}"))?;
        let mapped = slice.get_mapped_range();
        let mut output = vec![0u8; tw as usize * th as usize * 4];
        for y in 0..th as usize {
            let src_row = &mapped[y * padded_row as usize..y * padded_row as usize + tw as usize * 4];
            let dst_row = &mut output[y * tw as usize * 4..(y + 1) * tw as usize * 4];
            dst_row.copy_from_slice(src_row);
        }
        drop(mapped);
        readback.unmap();
        Ok(output)
    }
}
