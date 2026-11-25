//! Stella's native GPU preview renderer. FFmpeg supplies decoded source pixels;
//! all fitting, transform, colour grading, effects and compositing happen in wgpu.
mod params;
mod wgpu_renderer;

pub use params::LayerUniforms;
pub use wgpu_renderer::{set_host_gl_context, GpuLayerFrame, GpuPreviewRenderer, SharedGlPreviewFrame};
