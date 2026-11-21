use std::collections::HashMap;
use std::sync::{Arc, Mutex, OnceLock};

use wgpu::util::DeviceExt;

const SHADER: &str = r#"
struct PreviewUniforms {
    source_target: vec4<f32>, // source width/height, target width/height
    composite: vec4<f32>,     // opacity, rotation radians, transition progress, transition kind
    transform: vec4<f32>,     // scale x/y, position x/y in normalized canvas coordinates
    crop: vec4<f32>,          // left, top, right, bottom normalized source crop
    color: vec4<f32>,         // brightness, contrast, saturation, vibrance
    shadows: vec4<f32>,
    midtones: vec4<f32>,
    highlights: vec4<f32>,
    hsl: vec4<f32>,           // hue shift turns, saturation multiplier delta, lightness, selected hue center turns
    effects: vec4<f32>,       // vignette, sharpen, scanline amount, posterize levels (0 = off)
    effects2: vec4<f32>,      // pixelate size px, chromatic offset px, mirror, glow amount
    effects3: vec4<f32>,      // blur px, grain amount, film burn amount, shake px
    effects4: vec4<f32>,      // halftone amount, lens distortion, glitch amount, scanline spacing px
    transition: vec4<f32>,    // wipe angle x/y, transition softness, source-time seconds
    distort: vec4<f32>,       // top-left, top-right, bottom-right, bottom-left displacement
    effects5: vec4<f32>,      // light leak amount, warmth, horizontal position, reserved
};
@group(0) @binding(0) var source_texture: texture_2d<f32>;
@group(0) @binding(1) var source_sampler: sampler;
@group(0) @binding(2) var<uniform> u: PreviewUniforms;

struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOut {
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0)
    );
    let p = positions[index];
    var out: VertexOut;
    out.position = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return out;
}

fn rgb_to_hsv(c: vec3<f32>) -> vec3<f32> {
    let mx = max(c.r, max(c.g, c.b));
    let mn = min(c.r, min(c.g, c.b));
    let d = mx - mn;
    var h = 0.0;
    if (d > 0.00001) {
        if (mx == c.r) { h = (c.g - c.b) / d; if (h < 0.0) { h += 6.0; } }
        else if (mx == c.g) { h = (c.b - c.r) / d + 2.0; }
        else { h = (c.r - c.g) / d + 4.0; }
        h = h / 6.0;
    }
    let s = select(0.0, d / max(mx, 0.00001), mx > 0.00001);
    return vec3<f32>(h, s, mx);
}

fn hsv_to_rgb(c: vec3<f32>) -> vec3<f32> {
    let k = vec3<f32>(0.0, 4.0, 2.0);
    let p = abs(fract(c.xxx + k / 6.0) * 6.0 - 3.0);
    return c.z * mix(vec3<f32>(1.0), clamp(p - 1.0, vec3<f32>(0.0), vec3<f32>(1.0)), c.y);
}

fn hash21(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453);
}

fn sample_color(uv: vec2<f32>) -> vec3<f32> {
    let texel = 1.0 / u.source_target.xy;
    var c = textureSample(source_texture, source_sampler, uv).rgb;
    if (u.effects3.x > 0.01) {
        let o = texel * u.effects3.x;
        c = c * 0.4
          + textureSample(source_texture, source_sampler, uv + vec2<f32>(o.x, 0.0)).rgb * 0.15
          + textureSample(source_texture, source_sampler, uv - vec2<f32>(o.x, 0.0)).rgb * 0.15
          + textureSample(source_texture, source_sampler, uv + vec2<f32>(0.0, o.y)).rgb * 0.15
          + textureSample(source_texture, source_sampler, uv - vec2<f32>(0.0, o.y)).rgb * 0.15;
    }
    if (u.effects2.y > 0.01) {
        let shift = texel * u.effects2.y;
        let red = textureSample(source_texture, source_sampler, uv + shift).r;
        let blue = textureSample(source_texture, source_sampler, uv - shift).b;
        c = vec3<f32>(red, c.g, blue);
    }
    if (u.effects2.w > 0.001) {
        let bright = max(c - vec3<f32>(0.72), vec3<f32>(0.0));
        c += bright * u.effects2.w * 1.8;
    }
    return c;
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    let source_aspect = u.source_target.x / max(u.source_target.y, 1.0);
    let target_aspect = u.source_target.z / max(u.source_target.w, 1.0);
    var fitted = vec2<f32>(1.0, 1.0);
    if (source_aspect > target_aspect) { fitted.y = target_aspect / source_aspect; }
    else { fitted.x = source_aspect / target_aspect; }

    var p = in.uv - vec2<f32>(0.5, 0.5) - u.transform.zw;
    if (u.effects3.w > 0.01) {
        let t = u.transition.w;
        p += vec2<f32>(sin(t * 8.0 + in.position.y * 0.013), cos(t * 7.3 + in.position.x * 0.011)) * u.effects3.w / u.source_target.zw;
    }
    let cs = cos(-u.composite.y);
    let sn = sin(-u.composite.y);
    p = vec2<f32>(p.x * cs - p.y * sn, p.x * sn + p.y * cs);
    p = p / max(u.transform.xy, vec2<f32>(0.001, 0.001));
    if (u.composite.w == 4.0) {
        let slide_axis = normalize(u.transition.xy + vec2<f32>(0.0001, 0.0001));
        p += slide_axis * (1.0 - clamp(u.composite.z, 0.0, 1.0)) * 1.8;
    }
    var uv = p / fitted + vec2<f32>(0.5, 0.5);

    let crop_size = max(vec2<f32>(0.02, 0.02), vec2<f32>(1.0 - u.crop.x - u.crop.z, 1.0 - u.crop.y - u.crop.w));
    uv = vec2<f32>(u.crop.x, u.crop.y) + uv * crop_size;
    if (u.effects2.z > 0.5) { uv.x = 1.0 - uv.x; }

    // Mild corner displacement used by the four-point Distort controls.
    let top = mix(u.distort.x, u.distort.y, uv.x);
    let bottom = mix(u.distort.w, u.distort.z, uv.x);
    uv.y += mix(top, bottom, uv.y) * 0.001;

    if (u.effects4.y > 0.001) {
        let center = uv - vec2<f32>(0.5, 0.5);
        uv += center * dot(center, center) * u.effects4.y * 0.55;
    }
    if (u.effects2.x > 1.0) {
        let block = max(u.effects2.x, 1.0);
        let pixel = u.source_target.xy / block;
        uv = (floor(uv * pixel) + vec2<f32>(0.5, 0.5)) / pixel;
    }
    if (u.effects4.z > 0.001) {
        let stripe = floor(in.position.y / 8.0);
        uv.x += select(0.0, hash21(vec2<f32>(stripe, floor(in.position.x / 16.0))) - 0.5, fract(stripe * 0.17) < 0.32) * u.effects4.z * 0.08;
    }

    if (any(uv < vec2<f32>(0.0, 0.0)) || any(uv > vec2<f32>(1.0, 1.0))) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    // Incoming transition mask. Fade, wipe, iris, slide and dip are GPU-native.
    let progress = clamp(u.composite.z, 0.0, 1.0);
    var transition_alpha = 1.0;
    if (u.composite.w == 1.0 || u.composite.w == 5.0) { transition_alpha = progress; }
    else if (u.composite.w == 2.0) {
        let axis = normalize(u.transition.xy + vec2<f32>(0.0001, 0.0001));
        transition_alpha = 1.0 - smoothstep(progress - 0.015, progress + 0.015, dot(uv, axis));
    }
    else if (u.composite.w == 3.0) {
        let radius = distance(uv, vec2<f32>(0.5, 0.5));
        transition_alpha = 1.0 - smoothstep(progress * 0.72 - 0.02, progress * 0.72 + 0.02, radius);
    }

    var rgb = sample_color(uv);
    let luma = dot(rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
    rgb = (rgb + vec3<f32>(u.color.x) - vec3<f32>(0.5)) * u.color.y + vec3<f32>(0.5);
    let y = dot(rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
    rgb = vec3<f32>(y) + (rgb - vec3<f32>(y)) * u.color.z;
    let maxc = max(rgb.r, max(rgb.g, rgb.b));
    let minc = min(rgb.r, min(rgb.g, rgb.b));
    let sat = (maxc - minc) / max(maxc, 0.0001);
    let vib = 1.0 + u.color.w * (1.0 - sat);
    rgb = vec3<f32>(y) + (rgb - vec3<f32>(y)) * vib;

    // Selected HSL hue band.
    var hsv = rgb_to_hsv(max(rgb, vec3<f32>(0.0)));
    let hue_delta = abs(fract(hsv.x - u.hsl.w + 0.5) - 0.5);
    let hue_mask = select(1.0 - smoothstep(0.075, 0.19, hue_delta), 1.0, u.hsl.w < 0.0);
    hsv.x = fract(hsv.x + u.hsl.x * hue_mask);
    hsv.y = clamp(hsv.y * (1.0 + u.hsl.y * hue_mask), 0.0, 1.0);
    hsv.z = max(0.0, hsv.z + u.hsl.z * hue_mask);
    rgb = hsv_to_rgb(hsv);

    let luminance = dot(rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
    let shadow_weight = pow(1.0 - clamp(luminance, 0.0, 1.0), 2.0);
    let highlight_weight = pow(clamp(luminance, 0.0, 1.0), 2.0);
    let mid_weight = clamp(1.0 - shadow_weight - highlight_weight, 0.0, 1.0);
    rgb += u.shadows.rgb * shadow_weight + u.midtones.rgb * mid_weight + u.highlights.rgb * highlight_weight;

    if (u.effects.y > 0.001) {
        let texel = 1.0 / u.source_target.xy;
        let center = textureSample(source_texture, source_sampler, uv).rgb;
        let blur = (textureSample(source_texture, source_sampler, uv + vec2<f32>(texel.x, 0.0)).rgb
                  + textureSample(source_texture, source_sampler, uv - vec2<f32>(texel.x, 0.0)).rgb
                  + textureSample(source_texture, source_sampler, uv + vec2<f32>(0.0, texel.y)).rgb
                  + textureSample(source_texture, source_sampler, uv - vec2<f32>(0.0, texel.y)).rgb) * 0.25;
        rgb = center + (center - blur) * u.effects.y;
    }
    if (u.effects.w > 1.0) {
        let levels = max(u.effects.w - 1.0, 1.0);
        rgb = floor(clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0)) * levels + vec3<f32>(0.5)) / levels;
    }
    if (u.effects.z > 0.001) {
        let line = select(0.0, 1.0, (u.effects4.w <= 1.0) || (i32(in.position.y) % i32(max(u.effects4.w, 1.0)) == 0));
        rgb *= 1.0 - line * u.effects.z;
    }
    if (u.effects.x > 0.001) {
        let edge = distance(in.uv, vec2<f32>(0.5, 0.5)) * 1.414;
        rgb *= 1.0 - smoothstep(0.25, 0.95, edge) * u.effects.x;
    }
    if (u.effects3.y > 0.001) {
        rgb += vec3<f32>((hash21(in.position.xy) - 0.5) * u.effects3.y * 0.16);
    }
    if (u.effects3.z > 0.001) {
        let edge = smoothstep(0.38, 0.9, distance(in.uv, vec2<f32>(0.5, 0.5)));
        let flicker = 1.0 + sin(u.transition.w * 8.0 * 3.14159265) * u.effects5.w;
        rgb += vec3<f32>(1.0, 0.34, 0.08) * edge * u.effects3.z * 0.55 * flicker;
    }
    if (u.effects4.x > 0.001) {
        let dot_grid = abs(fract(in.position.x * 0.12) - 0.5) + abs(fract(in.position.y * 0.12) - 0.5);
        rgb *= mix(1.0, smoothstep(0.08, 0.55, dot_grid), u.effects4.x);
    }
    if (u.effects5.x > 0.001) {
        let spread = 0.14 + u.effects5.y * 0.38;
        let hotspot = max(0.0, 1.0 - abs(in.uv.x - u.effects5.z) / spread);
        let warmth = u.effects5.y;
        rgb += vec3<f32>(1.0, 0.25 + warmth * 0.2, (1.0 - warmth) * 0.25) * hotspot * u.effects5.x;
    }
    if (u.effects2.w > 0.001) {
        let thresholded = max(rgb - vec3<f32>(0.7), vec3<f32>(0.0));
        rgb += thresholded * u.effects2.w;
    }
    rgb = clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0));
    let alpha = textureSample(source_texture, source_sampler, uv).a * clamp(u.composite.x, 0.0, 1.0) * transition_alpha;
    return vec4<f32>(rgb, alpha);
}
"#;

#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct PreviewUniforms {
    pub source_target: [f32; 4],
    pub composite: [f32; 4],
    pub transform: [f32; 4],
    pub crop: [f32; 4],
    pub color: [f32; 4],
    pub shadows: [f32; 4],
    pub midtones: [f32; 4],
    pub highlights: [f32; 4],
    pub hsl: [f32; 4],
    pub effects: [f32; 4],
    pub effects2: [f32; 4],
    pub effects3: [f32; 4],
    pub effects4: [f32; 4],
    pub transition: [f32; 4],
    pub distort: [f32; 4],
    pub effects5: [f32; 4],
}

impl Default for PreviewUniforms {
    fn default() -> Self {
        Self {
            source_target: [1.0, 1.0, 1.0, 1.0], composite: [1.0, 0.0, 1.0, 0.0],
            transform: [1.0, 1.0, 0.0, 0.0], crop: [0.0; 4], color: [0.0, 1.0, 1.0, 0.0],
            shadows: [0.0; 4], midtones: [0.0; 4], highlights: [0.0; 4], hsl: [0.0, 0.0, 0.0, 0.0],
            effects: [0.0; 4], effects2: [0.0; 4], effects3: [0.0; 4], effects4: [0.0; 4],
            transition: [1.0, 0.0, 0.02, 0.0], distort: [0.0; 4], effects5: [0.0; 4],
        }
    }
}

struct LayerResources {
    width: u32,
    height: u32,
    _texture: wgpu::Texture,
    _view: wgpu::TextureView,
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
        Ok::<_, String>(RendererState { device: Arc::new(device), queue: Arc::new(queue), pipeline, sampler, layers: HashMap::new(), target: None })
    })?;
    let _ = GPU.set(Mutex::new(created));
    GPU.get().ok_or_else(|| "wgpu renderer initialization failed".to_string())
}

pub struct GpuLayerFrame<'a> {
    pub key: &'a str,
    pub pixels: &'a [u8],
    pub width: u32,
    pub height: u32,
    pub uniforms: PreviewUniforms,
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
            let recreate = renderer.layers.get(layer.key).is_none_or(|resource| resource.width != layer.width || resource.height != layer.height);
            if recreate {
                let texture = device.create_texture(&wgpu::TextureDescriptor {
                    label: Some("Stella persistent source texture"), size: wgpu::Extent3d { width: layer.width, height: layer.height, depth_or_array_layers: 1 },
                    mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
                    format: wgpu::TextureFormat::Rgba8Unorm,
                    usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST, view_formats: &[],
                });
                let view = texture.create_view(&Default::default());
                let uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                    label: Some("Stella persistent layer uniforms"), contents: bytemuck::bytes_of(&layer.uniforms),
                    usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                });
                let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
                    label: Some("Stella persistent layer bind group"), layout: &renderer.pipeline.get_bind_group_layout(0),
                    entries: &[
                        wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(&view) },
                        wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::Sampler(&renderer.sampler) },
                        wgpu::BindGroupEntry { binding: 2, resource: uniform_buffer.as_entire_binding() },
                    ],
                });
                renderer.layers.insert(layer.key.to_owned(), LayerResources {
                    width: layer.width, height: layer.height, _texture: texture, _view: view, uniform_buffer, bind_group,
                });
            }
            let resource = renderer.layers.get(layer.key).expect("layer resource was just created");
            queue.write_texture(wgpu::ImageCopyTexture { texture: &resource._texture, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
                layer.pixels, wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(layer.width * 4), rows_per_image: Some(layer.height) },
                wgpu::Extent3d { width: layer.width, height: layer.height, depth_or_array_layers: 1 });
            queue.write_buffer(&resource.uniform_buffer, 0, bytemuck::bytes_of(&layer.uniforms));
        }
        // Keep the cache bounded when many clips have been visited in a project.
        if renderer.layers.len() > 32 {
            let active: std::collections::HashSet<&str> = layers.iter().map(|layer| layer.key).collect();
            let stale: Vec<String> = renderer.layers.keys().filter(|key| !active.contains(key.as_str())).take(renderer.layers.len() - 24).cloned().collect();
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
