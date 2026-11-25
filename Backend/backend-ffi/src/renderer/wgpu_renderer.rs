use std::collections::HashMap;
use std::mem::size_of;
use std::num::NonZeroU64;
use std::sync::{
    atomic::{AtomicBool, AtomicUsize, Ordering},
    mpsc, Arc, Mutex, OnceLock,
};
use std::thread;
use std::time::Duration;

use super::params::LayerUniforms;

#[cfg(windows)]
use glow::HasContext;

const SHADER: &str = include_str!("preview.wgsl");
const FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba8Unorm;

/// Evicted source textures kept around for reuse (same size => no driver
/// allocation). Small on purpose so memory stays bounded.
const TEXTURE_POOL_MAX: usize = 2;
/// Previous source frames kept beyond the ones the current composite uses.
/// A fixed large cache could eat hundreds of MiB at 1080p with many layers.
const SOURCE_CACHE_EXTRA: usize = 4;
/// Initial number of layers the shared uniform buffer can hold (grows by x2).
const INITIAL_LAYER_CAPACITY: usize = 16;

struct SourceResources {
    width: u32,
    height: u32,
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    /// texture + sampler + the shared (dynamic-offset) uniform buffer. Built
    /// once per texture, so playback no longer creates a bind group per frame.
    bind_group: wgpu::BindGroup,
    /// Composite tick this source was last used on (for LRU eviction).
    last_used: u64,
}

struct ReadbackSlot {
    buffer: Arc<wgpu::Buffer>,
    busy: Arc<AtomicBool>,
}

struct TargetResources {
    width: u32,
    height: u32,
    padded_row: u32,
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    // Three staging buffers let independent preview requests be in flight at
    // once. Mapping completion is driven by the dedicated non-blocking poller.
    readbacks: Vec<ReadbackSlot>,
}

/// One WGPU-owned output texture in the Windows WGL share group. It is never
/// handed to Raylib as an owned texture; the UI releases its slot only after
/// a producer-side GPU wait is queued, or after glFinish completes its last draw.
struct SharedGlTargetSlot {
    width: u32,
    height: u32,
    texture: Option<wgpu::Texture>,
    view: Option<wgpu::TextureView>,
    gl_name: u32,
    token: u64,
    in_use: bool,
    /// Producer fence is owned by WGPU until the UI releases this slot.
    ready_fence: usize,
}

struct RendererState {
    device: Arc<wgpu::Device>,
    queue: Arc<wgpu::Queue>,
    pipeline: wgpu::RenderPipeline,
    bind_layout: wgpu::BindGroupLayout,
    sampler: wgpu::Sampler,
    // Source pixels are cached independently from clip/layer uniforms. Multiple
    // layers can therefore share one texture upload while retaining separate
    // transforms, opacity, keyframes and blend order.
    sources: HashMap<String, SourceResources>,
    // Recently evicted source textures, reused for new frames of the same size.
    texture_pool: Vec<SourceResources>,
    target: Option<TargetResources>,
    // Present only when the host WGL context was successfully joined to the
    // WGPU GLES context before WGPU created its first GL object.
    #[cfg(windows)]
    shared_gl_context: Option<wgpu::hal::gles::AdapterContext>,
    #[cfg(not(windows))]
    shared_gl_context: Option<()>,
    shared_gl_targets: Vec<SharedGlTargetSlot>,
    next_shared_gl_token: u64,
    // One uniform buffer for every layer of a frame. Layer `i` lives at
    // `i * uniform_stride` and is selected with a dynamic offset, so there is
    // no per-layer buffer, no per-layer map, and one write_buffer per frame.
    uniform_buffer: wgpu::Buffer,
    uniform_capacity: usize,
    uniform_stride: usize,
    uniform_scratch: Vec<u8>,
    tick: u64,
}

fn create_uniform_buffer(device: &wgpu::Device, stride: usize, capacity: usize) -> wgpu::Buffer {
    device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("Stella shared layer uniforms"),
        size: (stride * capacity) as u64,
        usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        mapped_at_creation: false,
    })
}

fn make_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    sampler: &wgpu::Sampler,
    view: &wgpu::TextureView,
    uniforms: &wgpu::Buffer,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("Stella source bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(view) },
            wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::Sampler(sampler) },
            wgpu::BindGroupEntry {
                binding: 2,
                resource: wgpu::BindingResource::Buffer(wgpu::BufferBinding {
                    buffer: uniforms,
                    offset: 0,
                    size: NonZeroU64::new(size_of::<LayerUniforms>() as u64),
                }),
            },
        ],
    })
}

impl RendererState {
    fn create_source(&self, width: u32, height: u32) -> SourceResources {
        let texture = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Stella shared preview source texture"),
            size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: FORMAT,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        let view = texture.create_view(&Default::default());
        let bind_group = make_bind_group(&self.device, &self.bind_layout, &self.sampler, &view, &self.uniform_buffer);
        SourceResources { width, height, texture, view, bind_group, last_used: 0 }
    }

    /// Grow the shared uniform buffer if needed. Bind groups reference the
    /// buffer, so every existing one (cached and pooled) is rebuilt. Rare.
    fn ensure_uniform_capacity(&mut self, layers: usize) {
        if layers <= self.uniform_capacity {
            return;
        }
        let capacity = layers.next_power_of_two();
        let buffer = create_uniform_buffer(&self.device, self.uniform_stride, capacity);
        {
            let Self { device, bind_layout, sampler, sources, texture_pool, .. } = &mut *self;
            for s in sources.values_mut().chain(texture_pool.iter_mut()) {
                s.bind_group = make_bind_group(device, bind_layout, sampler, &s.view, &buffer);
            }
        }
        self.uniform_buffer = buffer;
        self.uniform_capacity = capacity;
    }
}

// The host OpenGL context must be supplied before the first preview request.
// The renderer is process-global, so this is a one-time initialization input.
static HOST_GL_CONTEXT: AtomicUsize = AtomicUsize::new(0);
#[cfg(windows)]
static HOST_GL_DC: AtomicUsize = AtomicUsize::new(0);
static SHARED_GL_DISABLED: AtomicBool = AtomicBool::new(false);

/// Called by the Raylib bridge after InitWindow has made its WGL context current.
/// Returns false if the renderer was already initialized or the handle is null.
pub fn set_host_gl_context(context: usize, device_context: usize) -> bool {
    if context == 0 || device_context == 0 || GPU.get().is_some() ||
        HOST_GL_CONTEXT.load(Ordering::Acquire) != 0 {
        return false;
    }
    // Publish the HDC first; the release on HGLRC makes both values visible
    // to init_state's acquire load before WGPU creates its hidden WGL window.
    #[cfg(windows)]
    HOST_GL_DC.store(device_context, Ordering::Relaxed);
    HOST_GL_CONTEXT.compare_exchange(0, context, Ordering::AcqRel, Ordering::Acquire).is_ok()
}

// The result (including a failure) is cached: on machines where no adapter
// exists we report the error cheaply instead of re-probing the GPU every frame.
static GPU: OnceLock<Result<Mutex<RendererState>, String>> = OnceLock::new();

fn state() -> Result<&'static Mutex<RendererState>, String> {
    GPU.get_or_init(|| init_state().map(Mutex::new)).as_ref().map_err(|e| e.clone())
}

fn init_state() -> Result<RendererState, String> {
    pollster::block_on(async {
        let host_gl_context = HOST_GL_CONTEXT.load(Ordering::Acquire);
        #[cfg(windows)]
        let host_gl_dc = HOST_GL_DC.load(Ordering::Acquire);
        #[cfg(windows)]
        let wants_shared_gl = std::env::var_os("STELLA_DISABLE_WGPU_GL_INTEROP").is_none()
            && host_gl_context != 0 && host_gl_dc != 0;
        #[cfg(not(windows))]
        let wants_shared_gl = false;
        #[cfg(windows)]
        if wants_shared_gl {
            // The WGPU HAL creates a hidden WGL window. Give it Raylib's pixel
            // format before that window sets one; WGL context sharing requires
            // identical pixel formats across the two contexts.
            wgpu::hal::gles::set_host_pixel_format_dc(host_gl_dc as *mut std::ffi::c_void);
            // Pass Raylib's HGLRC to WGPU's WGL context constructor as its
            // native share context. This happens before WGPU allocates any GL
            // objects and remains valid even though Raylib already owns textures.
            wgpu::hal::gles::set_host_gl_share_context(host_gl_context as *mut std::ffi::c_void);
        }
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            // A shareable WGL object must be rendered by WGPU's GL backend.
            // Keep the existing best-backend selection when no host context is
            // provided (e.g. tests or non-Windows builds).
            backends: if wants_shared_gl { wgpu::Backends::GL } else { wgpu::Backends::all() },
            ..Default::default()
        });
        let adapter_options = wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: None,
            force_fallback_adapter: false,
        };
        let adapter_result = instance.request_adapter(&adapter_options).await;
        let (instance, adapter) = match adapter_result {
            Some(adapter) => (instance, adapter),
            None if wants_shared_gl => {
                // Some Windows systems expose no usable native WGL adapter (or
                // have a broken GL ICD). Do not make preview unavailable merely
                // because the zero-copy route cannot initialize. Clear the
                // interop request before retrying so an ordinary, unshared GL
                // adapter remains eligible for the CPU-readback fallback.
                eprintln!("Stella WGL adapter unavailable; retrying WGPU with the normal backend set");
                #[cfg(windows)]
                {
                    wgpu::hal::gles::set_host_gl_share_context(std::ptr::null_mut());
                    wgpu::hal::gles::set_host_pixel_format_dc(std::ptr::null_mut());
                }
                let fallback_instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
                    backends: wgpu::Backends::all(),
                    ..Default::default()
                });
                let fallback_adapter = fallback_instance.request_adapter(&adapter_options).await
                    .ok_or_else(|| "wgpu could not find a usable GPU adapter".to_string())?;
                (fallback_instance, fallback_adapter)
            }
            None => return Err("wgpu could not find a usable GPU adapter".to_string()),
        };
        let initial_info = adapter.get_info();

        // The patched WGL HAL joined the host share group at context creation,
        // before this adapter can allocate any GL objects. Retain a handle to
        // that exact context for explicit producer/consumer GLsync handoffs.
        #[cfg(windows)]
        let shared_gl_context = if wants_shared_gl && initial_info.backend == wgpu::Backend::Gl {
            let mut shared = None;
            unsafe {
                adapter.as_hal::<wgpu::hal::api::Gles, _, _>(|hal_adapter| {
                    if let Some(hal_adapter) = hal_adapter {
                        shared = Some(hal_adapter.adapter_context());
                    }
                });
            }
            shared
        } else {
            None
        };
        #[cfg(not(windows))]
        let shared_gl_context: Option<()> = None;

        // If a GL adapter exists but cannot join Raylib's share group, do not
        // keep forcing the readback path through that GL adapter. Re-probe the
        // normal backend set (D3D12/Vulkan on Windows) for the CPU-readback
        // fallback. The successful WGL path remains on the GL adapter.
        #[cfg(windows)]
        let (instance, adapter, shared_gl_context) =
            if wants_shared_gl && initial_info.backend == wgpu::Backend::Gl && shared_gl_context.is_none() {
                eprintln!("Stella WGL texture sharing failed; retrying WGPU with the normal backend set");
                drop(adapter);
                drop(instance);
                // Do not leak the rejected host share handles into the retry.
                // This allows a regular WGL context to serve readback if that is
                // the only usable backend on the machine.
                wgpu::hal::gles::set_host_gl_share_context(std::ptr::null_mut());
                wgpu::hal::gles::set_host_pixel_format_dc(std::ptr::null_mut());
                let fallback_instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
                    backends: wgpu::Backends::all(),
                    ..Default::default()
                });
                let fallback_adapter = fallback_instance.request_adapter(&adapter_options).await
                    .ok_or_else(|| "wgpu could not find a usable fallback GPU adapter".to_string())?;
                (fallback_instance, fallback_adapter, None)
            } else {
                (instance, adapter, shared_gl_context)
            };
        #[cfg(not(windows))]
        let (instance, adapter, shared_gl_context) = (instance, adapter, shared_gl_context);

        let info = adapter.get_info();
        eprintln!("wgpu adapter: {} | backend: {:?} | type: {:?} | driver: {}",
            info.name, info.backend, info.device_type, info.driver);

        if shared_gl_context.is_some() {
            eprintln!("Stella WGPU/OpenGL shared-texture interop enabled (WGL)");
        } else if wants_shared_gl {
            eprintln!("Stella WGPU/OpenGL interop unavailable; using asynchronous readback fallback");
        }

        let (device, queue) = adapter.request_device(&wgpu::DeviceDescriptor {
            label: Some("Stella preview device"),
            required_features: wgpu::Features::empty(),
            required_limits: adapter.limits(),
        }, None).await.map_err(|e| format!("wgpu device creation failed: {e}"))?;
        let device = Arc::new(device);
        let queue = Arc::new(queue);

        // Native wgpu backends require Device::poll to drive map_async callbacks.
        // Keep that work off the preview workers and never use Maintain::Wait in
        // the interactive renderer. A 1ms cadence keeps mapped frames moving
        // without spinning a core at 100% CPU.
        let poll_device = Arc::clone(&device);
        thread::Builder::new()
            .name("stella-wgpu-readback-poller".into())
            .spawn(move || loop {
                poll_device.poll(wgpu::Maintain::Poll);
                thread::sleep(Duration::from_millis(1));
            })
            .map_err(|e| format!("could not start WGPU readback poller: {e}"))?;

        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Stella real-time video compositor shader"),
            source: wgpu::ShaderSource::Wgsl(SHADER.into()),
        });
        let bind_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("Stella preview bind group layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: true,
                        min_binding_size: None,
                    },
                    count: None,
                },
            ],
        });
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("Stella preview pipeline layout"),
            bind_group_layouts: &[&bind_layout],
            push_constant_ranges: &[],
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Stella GPU preview compositor"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState { module: &shader, entry_point: "vs_main", buffers: &[] },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: "fs_main",
                targets: &[Some(wgpu::ColorTargetState {
                    format: FORMAT,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
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

        // Dynamic uniform offsets must be multiples of the device alignment.
        let align = (device.limits().min_uniform_buffer_offset_alignment as usize).max(1);
        let uniform_stride = size_of::<LayerUniforms>().div_ceil(align) * align;
        let uniform_buffer = create_uniform_buffer(&device, uniform_stride, INITIAL_LAYER_CAPACITY);

        Ok::<_, String>(RendererState {
            device,
            queue,
            pipeline,
            bind_layout,
            sampler,
            sources: HashMap::new(),
            texture_pool: Vec::new(),
            target: None,
            shared_gl_context,
            shared_gl_targets: (0..3).map(|_| SharedGlTargetSlot {
                width: 0, height: 0, texture: None, view: None, gl_name: 0,
                token: 0, in_use: false, ready_fence: 0,
            }).collect(),
            next_shared_gl_token: 1,
            uniform_buffer,
            uniform_capacity: INITIAL_LAYER_CAPACITY,
            uniform_stride,
            uniform_scratch: Vec::new(),
            tick: 0,
        })
    })
}

pub struct GpuLayerFrame<'a> {
    /// Layer identity. No longer needed for GPU resources (uniforms are slotted
    /// by draw order), kept so existing callers keep compiling.
    pub key: &'a str,
    /// Identity of the decoded source frame, shared by duplicate clip layers.
    pub source_key: &'a str,
    pub pixels: &'a [u8],
    pub width: u32,
    pub height: u32,
    pub uniforms: LayerUniforms,
}

/// A WGPU render target published as a native OpenGL texture. `ready_fence`
/// remains owned by WGPU; the host probes whether this driver's share group
/// exposes the sync object before attempting a cross-context GPU wait.
#[derive(Clone, Copy, Debug)]
pub struct SharedGlPreviewFrame {
    pub texture_id: u32,
    pub width: u32,
    pub height: u32,
    pub slot_token: u64,
    pub ready_fence: usize,
}

pub struct GpuPreviewRenderer;
impl GpuPreviewRenderer {
    pub fn composite_rgba(layers: &[GpuLayerFrame<'_>], tw: u32, th: u32) -> Result<Vec<u8>, String> {
        let receiver = Self::composite_rgba_async(layers, tw, th)?;
        receiver.recv().map_err(|e| format!("GPU readback channel closed: {e}"))?
    }

    /// Compatibility wrapper for synchronous export callers. Interactive
    /// preview uses `composite_rgba_async` and publishes the pixels later.
    pub fn composite_rgba_into(layers: &[GpuLayerFrame<'_>], tw: u32, th: u32, out: &mut Vec<u8>) -> Result<(), String> {
        let mut pixels = Self::composite_rgba(layers, tw, th)?;
        out.clear();
        std::mem::swap(out, &mut pixels);
        Ok(())
    }

    /// Submit the GPU composite and return immediately with a receiver for its
    /// readback. The command queue is ordered, each request gets its own staging
    /// buffer, and the WGPU state mutex is released before GPU completion.
    /// This allows the next preview worker to decode and submit another frame
    /// while the previous readback is still in flight.
    pub fn composite_rgba_async(
        layers: &[GpuLayerFrame<'_>],
        tw: u32,
        th: u32,
    ) -> Result<mpsc::Receiver<Result<Vec<u8>, String>>, String> {
        if tw == 0 || th == 0 { return Err("GPU preview dimensions must be nonzero".into()); }
        for layer in layers {
            if layer.width == 0 || layer.height == 0 { return Err("GPU layer dimensions must be nonzero".into()); }
            let expected = layer.width as usize * layer.height as usize * 4;
            if layer.pixels.len() != expected {
                return Err(format!("Decoded layer is {} bytes; expected {expected}", layer.pixels.len()));
            }
        }
        let mut guard = state()?.lock().map_err(|_| "wgpu renderer lock failed")?;
        // Reborrow once so disjoint fields can be borrowed independently.
        let r = &mut *guard;

        // Return an error instead of a wgpu validation panic on oversized input.
        let max_dim = r.device.limits().max_texture_dimension_2d;
        if tw > max_dim || th > max_dim || layers.iter().any(|l| l.width > max_dim || l.height > max_dim) {
            return Err(format!("GPU texture dimension exceeds device limit of {max_dim}"));
        }

        // Pack every layer's uniforms at its dynamic-offset slot. Padding bytes
        // stay zero, so this buffer doubles as the uniform part of the frame
        // signature.
        r.ensure_uniform_capacity(layers.len());
        let stride = r.uniform_stride;
        let ubytes = size_of::<LayerUniforms>();
        r.uniform_scratch.clear();
        r.uniform_scratch.resize(stride * layers.len(), 0);
        for (i, layer) in layers.iter().enumerate() {
            let u = layer.uniforms.with_sizes(layer.width, layer.height, tw, th);
            r.uniform_scratch[i * stride..i * stride + ubytes].copy_from_slice(bytemuck::bytes_of(&u));
        }

        if r.target.as_ref().is_none_or(|t| t.width != tw || t.height != th) {
            let texture = r.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Stella persistent preview target"),
                size: wgpu::Extent3d { width: tw, height: th, depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: FORMAT,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
                view_formats: &[],
            });
            let view = texture.create_view(&Default::default());
            let align = wgpu::COPY_BYTES_PER_ROW_ALIGNMENT;
            let padded_row = (tw * 4 + align - 1) & !(align - 1);
            let readbacks = (0..3).map(|slot| ReadbackSlot {
                buffer: Arc::new(r.device.create_buffer(&wgpu::BufferDescriptor {
                    label: Some(if slot == 0 { "Stella preview readback slot 0" } else if slot == 1 { "Stella preview readback slot 1" } else { "Stella preview readback slot 2" }),
                    size: padded_row as u64 * th as u64,
                    usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                    mapped_at_creation: false,
                })),
                busy: Arc::new(AtomicBool::new(false)),
            }).collect();
            r.target = Some(TargetResources { width: tw, height: th, padded_row, texture, view, readbacks });
        }

        // Upload layer uniforms: one write for the whole frame.
        if !r.uniform_scratch.is_empty() {
            r.queue.write_buffer(&r.uniform_buffer, 0, &r.uniform_scratch);
        }

        r.tick += 1;
        let tick = r.tick;
        let mut active_sources = 0usize;

        for layer in layers {
            // A source-frame texture is uploaded once, regardless of how many
            // timeline clips reference it. An existing key is already populated
            // with the same RGBA (dimensions are checked defensively).
            let cached = match r.sources.get_mut(layer.source_key) {
                Some(s) if s.width == layer.width && s.height == layer.height => {
                    if s.last_used != tick {
                        s.last_used = tick;
                        active_sources += 1;
                    }
                    true
                }
                _ => false,
            };
            if cached { continue; }

            // Prefer recycling an evicted texture of the same size: avoids a
            // driver allocation per frame. write_texture overwrites every
            // texel, so the result is identical to a fresh texture. Its bind
            // group stays valid too (same view, same shared uniform buffer).
            let pooled_index = r.texture_pool.iter()
                .position(|s| s.width == layer.width && s.height == layer.height);
            let mut source = match pooled_index {
                Some(index) => r.texture_pool.swap_remove(index),
                None => r.create_source(layer.width, layer.height),
            };
            source.last_used = tick;
            r.queue.write_texture(
                wgpu::ImageCopyTexture { texture: &source.texture, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
                layer.pixels,
                wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(layer.width * 4), rows_per_image: Some(layer.height) },
                wgpu::Extent3d { width: layer.width, height: layer.height, depth_or_array_layers: 1 },
            );
            r.sources.insert(layer.source_key.to_owned(), source);
            active_sources += 1;
        }

        // Evict the least-recently-used inactive sources beyond the working set
        // (previously the victims were arbitrary HashMap order, which could
        // drop the newest frame and keep a stale one). Never evicts a source
        // used by this composite.
        while r.sources.len() > active_sources + SOURCE_CACHE_EXTRA {
            let oldest = r.sources.iter()
                .filter(|(_, s)| s.last_used != tick)
                .min_by_key(|(_, s)| s.last_used)
                .map(|(k, _)| k.clone());
            let Some(oldest) = oldest else { break };
            if let Some(source) = r.sources.remove(&oldest) {
                if r.texture_pool.len() < TEXTURE_POOL_MAX {
                    r.texture_pool.push(source);
                }
            }
        }

        let target = r.target.as_ref().expect("persistent target initialized");
        let slot_index = target.readbacks.iter().position(|slot| {
            slot.busy.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).is_ok()
        }).ok_or_else(|| "All three WGPU preview readback slots are busy".to_owned())?;
        let readback = Arc::clone(&target.readbacks[slot_index].buffer);
        let slot_busy = Arc::clone(&target.readbacks[slot_index].busy);
        let padded_row = target.padded_row;
        let mut encoder = r.device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: Some("Stella asynchronous preview encoder") });
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Stella GPU composite pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target.view,
                    resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK), store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None,
                occlusion_query_set: None,
                timestamp_writes: None,
            });
            pass.set_pipeline(&r.pipeline);
            for (i, layer) in layers.iter().enumerate() {
                if let Some(source) = r.sources.get(layer.source_key) {
                    pass.set_bind_group(0, &source.bind_group, &[(i * stride) as u32]);
                    pass.draw(0..3, 0..1);
                }
            }
        }
        encoder.copy_texture_to_buffer(
            wgpu::ImageCopyTexture { texture: &target.texture, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
            wgpu::ImageCopyBuffer { buffer: &readback, layout: wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(padded_row), rows_per_image: Some(th) } },
            wgpu::Extent3d { width: tw, height: th, depth_or_array_layers: 1 },
        );
        r.queue.submit(Some(encoder.finish()));

        let (tx, rx) = mpsc::channel();
        let callback_buffer = Arc::clone(&readback);
        let row_bytes = tw as usize * 4;
        let padded_row_usize = padded_row as usize;
        let total = row_bytes * th as usize;
        readback.slice(..).map_async(wgpu::MapMode::Read, move |map_result| {
            let result = match map_result {
                Ok(()) => {
                    let mapped = callback_buffer.slice(..).get_mapped_range();
                    let mut pixels = crate::acquire_preview_vec();
                    pixels.clear();
                    if pixels.capacity() < total { pixels.reserve(total); }
                    if padded_row_usize == row_bytes {
                        pixels.extend_from_slice(&mapped[..total]);
                    } else {
                        for y in 0..th as usize {
                            pixels.extend_from_slice(&mapped[y * padded_row_usize..y * padded_row_usize + row_bytes]);
                        }
                    }
                    drop(mapped);
                    callback_buffer.unmap();
                    Ok(pixels)
                }
                Err(error) => Err(format!("GPU readback mapping failed: {error}")),
            };
            slot_busy.store(false, Ordering::Release);
            let _ = tx.send(result);
        });
        // The renderer lock is deliberately released here. Other preview tasks
        // may submit work and use the other readback slots while this one maps.
        drop(guard);
        Ok(rx)
    }

    /// Composite directly into a WGPU texture that belongs to the same WGL
    /// share group as Raylib. No copy-to-buffer, CPU pixel allocation, or
    /// texture re-upload occurs on this path.
    #[cfg(windows)]
    pub fn composite_shared_gl(
        layers: &[GpuLayerFrame<'_>],
        tw: u32,
        th: u32,
    ) -> Result<SharedGlPreviewFrame, String> {
        if tw == 0 || th == 0 { return Err("GPU preview dimensions must be nonzero".into()); }
        for layer in layers {
            if layer.width == 0 || layer.height == 0 { return Err("GPU layer dimensions must be nonzero".into()); }
            let expected = layer.width as usize * layer.height as usize * 4;
            if layer.pixels.len() != expected {
                return Err(format!("Decoded layer is {} bytes; expected {expected}", layer.pixels.len()));
            }
        }

        let mut guard = state()?.lock().map_err(|_| "wgpu renderer lock failed")?;
        let r = &mut *guard;
        let shared_context = r.shared_gl_context.as_ref().cloned()
            .ok_or_else(|| "WGPU/OpenGL shared context is not initialized".to_owned())?;
        let max_dim = r.device.limits().max_texture_dimension_2d;
        if tw > max_dim || th > max_dim || layers.iter().any(|l| l.width > max_dim || l.height > max_dim) {
            return Err(format!("GPU texture dimension exceeds device limit of {max_dim}"));
        }

        // Keep the same shared source-texture cache and dynamic uniform upload
        // path used by the CPU-readback renderer.
        r.ensure_uniform_capacity(layers.len());
        let stride = r.uniform_stride;
        let ubytes = size_of::<LayerUniforms>();
        r.uniform_scratch.clear();
        r.uniform_scratch.resize(stride * layers.len(), 0);
        for (i, layer) in layers.iter().enumerate() {
            let u = layer.uniforms.with_sizes(layer.width, layer.height, tw, th);
            r.uniform_scratch[i * stride..i * stride + ubytes].copy_from_slice(bytemuck::bytes_of(&u));
        }
        if !r.uniform_scratch.is_empty() {
            r.queue.write_buffer(&r.uniform_buffer, 0, &r.uniform_scratch);
        }

        r.tick += 1;
        let tick = r.tick;
        let mut active_sources = 0usize;
        for layer in layers {
            let cached = match r.sources.get_mut(layer.source_key) {
                Some(source) if source.width == layer.width && source.height == layer.height => {
                    if source.last_used != tick {
                        source.last_used = tick;
                        active_sources += 1;
                    }
                    true
                }
                _ => false,
            };
            if cached { continue; }
            let pooled_index = r.texture_pool.iter()
                .position(|source| source.width == layer.width && source.height == layer.height);
            let mut source = match pooled_index {
                Some(index) => r.texture_pool.swap_remove(index),
                None => r.create_source(layer.width, layer.height),
            };
            source.last_used = tick;
            r.queue.write_texture(
                wgpu::ImageCopyTexture { texture: &source.texture, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
                layer.pixels,
                wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(layer.width * 4), rows_per_image: Some(layer.height) },
                wgpu::Extent3d { width: layer.width, height: layer.height, depth_or_array_layers: 1 },
            );
            r.sources.insert(layer.source_key.to_owned(), source);
            active_sources += 1;
        }
        while r.sources.len() > active_sources + SOURCE_CACHE_EXTRA {
            let oldest = r.sources.iter()
                .filter(|(_, source)| source.last_used != tick)
                .min_by_key(|(_, source)| source.last_used)
                .map(|(key, _)| key.clone());
            let Some(oldest) = oldest else { break };
            if let Some(source) = r.sources.remove(&oldest) {
                if r.texture_pool.len() < TEXTURE_POOL_MAX { r.texture_pool.push(source); }
            }
        }

        // The UI marks a slot reusable only after WGPU has queued a wait on its
        // post-draw fence, or after the host has used the glFinish fallback.
        let slot_index = r.shared_gl_targets.iter().position(|slot| !slot.in_use)
            .ok_or_else(|| "All three shared OpenGL preview textures are still in use".to_owned())?;
        if r.shared_gl_targets[slot_index].ready_fence != 0 {
            return Err("Shared OpenGL preview slot still owns an unreleased producer fence".to_owned());
        }

        if r.shared_gl_targets[slot_index].width != tw || r.shared_gl_targets[slot_index].height != th ||
            r.shared_gl_targets[slot_index].texture.is_none() {
            let texture = r.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Stella zero-copy shared OpenGL preview target"),
                size: wgpu::Extent3d { width: tw, height: th, depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: FORMAT,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
                view_formats: &[],
            });
            let mut gl_name = None;
            unsafe {
                texture.as_hal::<wgpu::hal::api::Gles, _>(|hal_texture| {
                    if let Some(hal_texture) = hal_texture {
                        if let wgpu::hal::gles::TextureInner::Texture { raw, target } = &hal_texture.inner {
                            if *target == glow::TEXTURE_2D {
                                gl_name = Some(raw.0.get());
                            }
                        }
                    }
                });
            }
            let gl_name = gl_name.ok_or_else(|| "WGPU did not expose a shareable GL_TEXTURE_2D".to_owned())?;
            let view = texture.create_view(&Default::default());
            let slot = &mut r.shared_gl_targets[slot_index];
            slot.width = tw;
            slot.height = th;
            slot.texture = Some(texture);
            slot.view = Some(view);
            slot.gl_name = gl_name;
        }

        let texture_view = r.shared_gl_targets[slot_index].view.as_ref().expect("shared target view initialized");
        let mut encoder = r.device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Stella shared WGPU/OpenGL preview encoder"),
        });
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Stella shared GPU composite pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: texture_view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                occlusion_query_set: None,
                timestamp_writes: None,
            });
            pass.set_pipeline(&r.pipeline);
            for (index, layer) in layers.iter().enumerate() {
                if let Some(source) = r.sources.get(layer.source_key) {
                    pass.set_bind_group(0, &source.bind_group, &[(index * stride) as u32]);
                    pass.draw(0..3, 0..1);
                }
            }
        }
        r.queue.submit(Some(encoder.finish()));

        // Fence the producer command stream and flush it. The host probes
        // glIsSync before using a cross-context wait; drivers which do not share
        // sync objects use finish_shared_gl_frame instead.
        let ready_fence = {
            let gl = shared_context.lock();
            let fence = unsafe { gl.fence_sync(glow::SYNC_GPU_COMMANDS_COMPLETE, 0) }
                .map_err(|error| format!("could not create preview-ready fence: {error}"))?;
            unsafe { gl.flush() };
            fence.0 as usize
        };

        let token = r.next_shared_gl_token;
        r.next_shared_gl_token = r.next_shared_gl_token.wrapping_add(1).max(1);
        let slot = &mut r.shared_gl_targets[slot_index];
        slot.token = token;
        slot.in_use = true;
        slot.ready_fence = ready_fence;
        let frame = SharedGlPreviewFrame {
            texture_id: slot.gl_name,
            width: tw,
            height: th,
            slot_token: token,
            ready_fence,
        };
        drop(guard);
        Ok(frame)
    }

    #[cfg(not(windows))]
    pub fn composite_shared_gl(
        _layers: &[GpuLayerFrame<'_>],
        _tw: u32,
        _th: u32,
    ) -> Result<SharedGlPreviewFrame, String> {
        Err("WGPU/OpenGL texture sharing is only available on Windows WGL".to_owned())
    }

    /// Discard a shared frame that the UI never sampled. The producer fence
    /// is owned and deleted by WGPU before its slot becomes reusable.
    pub fn discard_shared_gl_frame(frame: SharedGlPreviewFrame) -> Result<bool, String> {
        let mut guard = state()?.lock().map_err(|_| "wgpu renderer lock failed")?;
        let Some(slot_index) = guard.shared_gl_targets.iter().position(|slot|
            slot.in_use && slot.token == frame.slot_token) else {
            return Ok(false);
        };
        let fence = guard.shared_gl_targets[slot_index].ready_fence;
        #[cfg(windows)]
        {
            // A stale frame was never sampled by the UI. It is still unsafe to
            // reuse its output texture until the producer's render has finished.
            // Complete only this rare discard path synchronously; normal visible
            // frames use the cross-context fence handoff below.
            let context = guard.shared_gl_context.as_ref().cloned()
                .ok_or_else(|| "WGPU/OpenGL shared context is unavailable".to_owned())?;
            let gl = context.lock();
            unsafe {
                gl.finish();
                if fence != 0 { gl.delete_sync(glow::NativeFence(fence as *mut _)); }
            }
        }
        let slot = &mut guard.shared_gl_targets[slot_index];
        slot.ready_fence = 0;
        slot.in_use = false;
        Ok(true)
    }

    /// Fallback when Raylib cannot see WGPU's producer GLsync through the WGL
    /// share group. Finish only the producer context, keeping the same texture
    /// and avoiding a CPU readback or texture upload.
    #[cfg(windows)]
    pub fn finish_shared_gl_frame(token: u64) -> Result<bool, String> {
        let mut guard = state()?.lock().map_err(|_| "wgpu renderer lock failed")?;
        let Some(slot_index) = guard.shared_gl_targets.iter().position(|slot|
            slot.in_use && slot.token == token) else {
            return Ok(false);
        };
        let fence = guard.shared_gl_targets[slot_index].ready_fence;
        if let Some(context) = guard.shared_gl_context.as_ref().cloned() {
            let gl = context.lock();
            unsafe {
                gl.finish();
                if fence != 0 { gl.delete_sync(glow::NativeFence(fence as *mut _)); }
            }
        } else {
            return Err("WGPU/OpenGL shared context is unavailable".to_owned());
        }
        guard.shared_gl_targets[slot_index].ready_fence = 0;
        Ok(true)
    }

    /// Try a non-blocking reverse handoff when the host sync object is visible
    /// in WGPU's context. A server-side wait orders future reuse without making
    /// the UI wait for completion. Returns false when this driver does not share
    /// GLsync objects; the caller then uses glFinish and release_shared_gl_frame.
    #[cfg(windows)]
    pub fn try_release_shared_gl_frame_with_fence(token: u64, ui_fence: usize) -> Result<bool, String> {
        if ui_fence == 0 { return Ok(false); }
        let mut guard = state()?.lock().map_err(|_| "wgpu renderer lock failed")?;
        let Some(slot_index) = guard.shared_gl_targets.iter().position(|slot|
            slot.in_use && slot.token == token) else {
            return Ok(false);
        };
        let context = guard.shared_gl_context.as_ref().cloned()
            .ok_or_else(|| "WGPU/OpenGL shared context is unavailable".to_owned())?;
        let gl = context.lock();
        let ui_sync = glow::NativeFence(ui_fence as *mut _);
        if !unsafe { gl.is_sync(ui_sync) } {
            return Ok(false);
        }
        unsafe {
            gl.wait_sync(ui_sync, 0, glow::TIMEOUT_IGNORED);
            gl.delete_sync(ui_sync);
        }
        let producer_fence = guard.shared_gl_targets[slot_index].ready_fence;
        if producer_fence != 0 {
            unsafe { gl.delete_sync(glow::NativeFence(producer_fence as *mut _)); }
        }
        drop(gl);
        let slot = &mut guard.shared_gl_targets[slot_index];
        slot.ready_fence = 0;
        slot.in_use = false;
        Ok(true)
    }

    /// Release a displayed texture after the host has called glFinish following
    /// its final draw. The producer fence is deleted in its owning context.
    pub fn release_shared_gl_frame(token: u64) -> Result<bool, String> {
        let mut guard = state()?.lock().map_err(|_| "wgpu renderer lock failed")?;
        let Some(slot_index) = guard.shared_gl_targets.iter().position(|slot|
            slot.in_use && slot.token == token) else {
            return Ok(false);
        };
        let fence = guard.shared_gl_targets[slot_index].ready_fence;
        if fence != 0 {
            #[cfg(windows)]
            if let Some(context) = guard.shared_gl_context.as_ref().cloned() {
                let gl = context.lock();
                unsafe { gl.delete_sync(glow::NativeFence(fence as *mut _)) };
            }
        }
        let slot = &mut guard.shared_gl_targets[slot_index];
        slot.ready_fence = 0;
        slot.in_use = false;
        Ok(true)
    }

    /// Disable shared-texture presentation for this process after a host-side
    /// visibility check fails. The initialized WGPU renderer remains valid and
    /// future frames use the asynchronous CPU-readback fallback.
    pub fn disable_shared_gl() {
        SHARED_GL_DISABLED.store(true, Ordering::Release);
    }

    pub fn shared_gl_available() -> bool {
        if SHARED_GL_DISABLED.load(Ordering::Acquire) {
            return false;
        }
        state().ok().and_then(|mutex| mutex.lock().ok().map(|state| state.shared_gl_context.is_some())).unwrap_or(false)
    }
}
