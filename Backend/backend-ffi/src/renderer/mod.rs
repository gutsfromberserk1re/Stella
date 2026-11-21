//! Stella's native GPU preview renderer. FFmpeg supplies decoded source pixels;
//! all preview fitting and display composition happens in wgpu.
mod wgpu_renderer;


pub use wgpu_renderer::{GpuLayerFrame, GpuPreviewRenderer};
