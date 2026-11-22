//! Maps a clip's `props` JSON onto the preview shader's uniform block.
//!
//! The property tree is the one the C inspector writes (`props.transform.*`,
//! `props.colors.*`, `props.effects.*`) and that `filters.rs` reads for export,
//! so a control only has to be wired in two places: here (preview) and there
//! (export). Keep the key names and defaults identical to `filters.rs`.
//!
//! Layout rule: every field is a `[f32; 4]` so the Rust struct has exactly the
//! same std140 layout as `Params` in preview.wgsl (no padding surprises).
use serde_json::Value;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, bytemuck::Pod, bytemuck::Zeroable)]
pub struct LayerUniforms {
    pub size: [f32; 4],  // src_w, src_h, target_w, target_h (filled by the renderer)
    pub geo0: [f32; 4],  // scale_x, scale_y, rotation_rad, opacity
    pub geo1: [f32; 4],  // pos_x, pos_y (fractions of target), mirror, time_sec
    pub crop: [f32; 4],  // left, right, top, bottom
    pub transition: [f32; 4], // axis_x, axis_y, progress, kind (0 none, 1 fade, 2 wipe, 3 iris, 4 slide, 5 dip)
    pub distort: [f32; 4], // top-left, top-right, bottom-right, bottom-left
    pub light: [f32; 4], // brightness, contrast, saturation, vibrance
    pub bal_s: [f32; 4],
    pub bal_m: [f32; 4],
    pub bal_h: [f32; 4],
    pub hsl: [f32; 4],   // hue_deg, sat, lightness, range_index
    pub fx0: [f32; 4],   // sharpen_amount, sharpen_radius_px, blur_value, blur_mode
    pub fx1: [f32; 4],   // blur_angle, vignette_amount, vignette_mid, vignette_feather
    pub fx2: [f32; 4],   // vignette_aspect, grain_amount, grain_block_px, chroma_px
    pub fx3: [f32; 4],   // chroma_angle, glow_sigma, glow_brightness, glow_contrast
    pub fx4: [f32; 4],   // pixelate_px, posterize_levels, posterize_blend, unused
    pub fx5: [f32; 4],   // lens_k, lens_zoom, scan_spacing, scan_amount
    pub fx6: [f32; 4],   // glitch_px, glitch_speed, halftone_size, halftone_contrast
    pub fx7: [f32; 4],   // halftone_angle, burn_amount, burn_warmth, burn_spread
    pub fx8: [f32; 4],   // burn_flicker, leak_amount, leak_warmth, leak_position
}

fn n(value: &Value, key: &str, fallback: f64) -> f64 {
    value[key].as_f64().filter(|v| v.is_finite()).unwrap_or(fallback)
}
fn on(effects: &Value, key: &str) -> bool {
    effects[key]["enabled"].as_bool().unwrap_or(false)
}
fn flag(value: bool) -> f32 {
    if value { 1.0 } else { 0.0 }
}

fn resolve_keyframed_props(props: &Value, keyframes: &Value, time: f64) -> Value {
    const FIELDS: [(&str, &[&str]); 6] = [
        ("x", &["transform", "position", "x"]),
        ("y", &["transform", "position", "y"]),
        ("scaleW", &["transform", "scale", "w"]),
        ("scaleH", &["transform", "scale", "h"]),
        ("rotation", &["transform", "rotation"]),
        ("opacity", &["transform", "opacity"]),
    ];
    let Some(frames) = keyframes.as_array().filter(|items| !items.is_empty()) else { return props.clone(); };
    let mut resolved = if props.is_object() { props.clone() } else { serde_json::json!({}) };
    for (field, path) in FIELDS {
        let mut points: Vec<(f64, f64)> = frames.iter().filter_map(|frame| {
            let at = frame["time"].as_f64().filter(|v| v.is_finite())?;
            let value = frame["values"][field].as_f64().filter(|v| v.is_finite())?;
            // Older Stella builds stored opacity as a 0..1 fraction; current
            // keyframes store the inspector's 0..100 percentage value.
            let value = if field == "opacity" && value > 0.0 && value <= 1.0 { value * 100.0 } else { value };
            Some((at, value))
        }).collect();
        if points.is_empty() { continue; }
        points.sort_by(|a, b| a.0.total_cmp(&b.0));
        let value = if time <= points[0].0 {
            points[0].1
        } else if time >= points[points.len() - 1].0 {
            points[points.len() - 1].1
        } else {
            let right = points.partition_point(|point| point.0 < time).min(points.len() - 1);
            let (t0, v0) = points[right - 1];
            let (t1, v1) = points[right];
            let amount = ((time - t0) / (t1 - t0).max(1e-9)).clamp(0.0, 1.0);
            v0 + (v1 - v0) * amount
        };
        let mut cursor = &mut resolved;
        for key in &path[..path.len() - 1] {
            if !cursor[*key].is_object() { cursor[*key] = serde_json::json!({}); }
            cursor = &mut cursor[*key];
        }
        cursor[path[path.len() - 1]] = serde_json::json!(value);
    }
    resolved
}

impl LayerUniforms {
    /// A layer that renders exactly like the old fit-and-blend shader.
    pub fn identity() -> Self {
        Self {
            size: [1.0, 1.0, 1.0, 1.0],
            geo0: [1.0, 1.0, 0.0, 1.0],
            geo1: [0.0, 0.0, 0.0, 0.0],
            crop: [0.0, 0.0, 0.0, 0.0],
            transition: [1.0, 0.0, 1.0, 0.0],
            distort: [0.0; 4],
            light: [0.0, 1.0, 1.0, 0.0],
            bal_s: [0.0; 4],
            bal_m: [0.0; 4],
            bal_h: [0.0; 4],
            hsl: [0.0, 0.0, 0.0, 0.0],
            fx0: [0.0, 2.5, 0.0, 0.0],
            fx1: [0.0, 0.0, 0.55, 0.5],
            fx2: [1.0, 0.0, 1.0, 0.0],
            fx3: [0.0, 0.0, 0.0, 1.0],
            fx4: [0.0, 0.0, 1.0, 0.0],
            fx5: [0.0, 1.0, 0.0, 0.0],
            fx6: [0.0, 1.0, 0.0, 0.5],
            fx7: [0.0, 0.0, 0.0, 0.5],
            fx8: [0.0, 0.0, 0.0, 0.0],
        }
    }

    pub fn opacity(&self) -> f32 {
        self.geo0[3]
    }

    pub fn with_sizes(mut self, src_w: u32, src_h: u32, target_w: u32, target_h: u32) -> Self {
        self.size = [src_w as f32, src_h as f32, target_w as f32, target_h as f32];
        self
    }

    /// `props` is `clip.extra["props"]` (or `Value::Null`); `time` is timeline seconds.
    pub fn from_props(props: &Value, time: f64) -> Self {
        Self::from_props_with_transition(props, time, None)
    }

    /// Resolves transform keyframes before mapping the normal property tree to
    /// uniforms. Keyframe times are absolute timeline seconds and values use
    /// x, y, scaleW, scaleH, rotation (degrees), and opacity (percent).
    pub fn from_props_with_keyframes(props: &Value, time: f64, keyframes: &Value) -> Self {
        let resolved = resolve_keyframed_props(props, keyframes, time);
        Self::from_props(&resolved, time)
    }

    /// Maps the active incoming transition along with the clip's visual properties.
    pub fn from_props_with_transition(
        props: &Value,
        time: f64,
        transition_in: Option<(&str, f32)>,
    ) -> Self {
        let mut u = Self::identity();
        let transform = &props["transform"];
        let effects = &props["effects"];
        let colors = &props["colors"];
        let light = &colors["light"];
        let color = &colors["color"];
        let detail = &colors["detail"];

        let preset = effects["preset"]["id"].as_str().unwrap_or("none");
        let preset_intensity = (n(&effects["preset"], "intensity", 100.0) / 100.0).clamp(0.0, 1.0);

        // ------------------------------------------------------------ transform
        let scale_w = (n(&transform["scale"], "w", 100.0) / 100.0).clamp(0.01, 10.0);
        let scale_h = (n(&transform["scale"], "h", 100.0) / 100.0).clamp(0.01, 10.0);
        let rotation = n(transform, "rotation", 0.0).to_radians();
        let opacity = (n(transform, "opacity", 100.0) / 100.0).clamp(0.0, 1.0);
        u.geo0 = [scale_w as f32, scale_h as f32, rotation as f32, opacity as f32];

        // Position is stored in 1920x1080 space; shake is added here per frame.
        let mut pos_x = n(&transform["position"], "x", 0.0);
        let mut pos_y = n(&transform["position"], "y", 0.0);
        if on(effects, "shake") {
            let shake = &effects["shake"];
            let amount = n(shake, "amount", 18.0) * 0.18;
            let freq = (n(shake, "frequency", 8.0) * (1.0 - n(shake, "smoothness", 35.0) / 120.0)).max(0.5);
            let phase = std::f64::consts::TAU * time * freq;
            pos_x += phase.sin() * amount;
            pos_y += phase.cos() * amount;
        }
        u.geo1 = [(pos_x / 1920.0) as f32, (pos_y / 1080.0) as f32, flag(on(effects, "mirror")), time as f32];

        let crop = &transform["crop"];
        u.crop = [
            (n(crop, "left", 0.0) / 1920.0).clamp(0.0, 0.45) as f32,
            (n(crop, "right", 0.0) / 1920.0).clamp(0.0, 0.45) as f32,
            (n(crop, "top", 0.0) / 1080.0).clamp(0.0, 0.45) as f32,
            (n(crop, "bottom", 0.0) / 1080.0).clamp(0.0, 0.45) as f32,
        ];

        let distort = &transform["distort"];
        u.distort = [
            n(distort, "tl", 0.0) as f32,
            n(distort, "tr", 0.0) as f32,
            n(distort, "br", 0.0) as f32,
            n(distort, "bl", 0.0) as f32,
        ];

        if let Some((kind, progress)) = transition_in {
            let transition_kind = match kind {
                "fade" | "crossfade" | "dissolve" | "fadeblack" | "fadewhite" => 1.0,
                "wipe" | "wipeleft" | "wiperight" | "wipeup" | "wipedown"
                | "diagtl" | "diagtr" | "diagbl" | "diagbr" => 2.0,
                "iris" | "circleopen" | "circleclose" | "radial" => 3.0,
                "slide" | "slideleft" | "slideright" | "slideup" | "slidedown" => 4.0,
                "dip" => 5.0,
                _ => 1.0,
            };
            let axis = match kind {
                "wipeup" | "slideup" => [0.0, 1.0],
                "wipedown" | "slidedown" => [0.0, -1.0],
                "wiperight" | "slideright" => [-1.0, 0.0],
                "diagtl" => [1.0, 1.0],
                "diagtr" => [-1.0, 1.0],
                "diagbl" => [1.0, -1.0],
                "diagbr" => [-1.0, -1.0],
                _ => [1.0, 0.0],
            };
            u.transition = [axis[0], axis[1], progress.clamp(0.0, 1.0), transition_kind];
        }

        // ---------------------------------------------------------------- light
        let mut brightness = (n(light, "exposure", 0.0) * 0.5 + n(light, "brightness", 0.0) / 100.0 * 0.25).clamp(-1.0, 1.0);
        let mut contrast = (1.0 + n(light, "contrast", 0.0) / 100.0 + n(detail, "clarity", 0.0) / 180.0).clamp(0.0, 4.0);
        let mut saturation = (1.0 + n(color, "saturation", 0.0) / 100.0).clamp(0.0, 3.0);
        let vibrance = (n(color, "vibrance", 0.0) / 100.0).clamp(-1.0, 1.0);
        let mut grain_levels = 0.0_f64; // noise strength in 0..255 levels

        match preset {
            "vivid" => { saturation *= 1.0 + 0.7 * preset_intensity; contrast *= 1.08; }
            "noir" => { saturation *= 1.0 - preset_intensity; contrast *= 1.0 + 0.2 * preset_intensity; brightness -= 0.02 * preset_intensity; }
            "fade" => { contrast *= 1.0 - 0.16 * preset_intensity; brightness += 0.08 * preset_intensity; saturation *= 1.0 - 0.2 * preset_intensity; }
            "cinema" => { contrast *= 1.0 + 0.15 * preset_intensity; saturation *= 1.0 - 0.15 * preset_intensity; }
            "retro" => { grain_levels += 12.0 * preset_intensity; }
            _ => {}
        }
        u.light = [brightness as f32, contrast as f32, saturation as f32, vibrance as f32];

        // ------------------------------------------------------------------ HSL
        let hsl_index = n(&colors["hsl"], "selected", 0.0).round().clamp(0.0, 8.0);
        let hsl_key = (hsl_index as usize).to_string();
        let hsl = &colors["hsl"][hsl_key.as_str()];
        u.hsl = [
            n(hsl, "hue", 0.0) as f32,
            (n(hsl, "saturation", 0.0) / 100.0).clamp(-1.0, 1.0) as f32,
            (n(hsl, "lightness", 0.0) / 200.0).clamp(-0.5, 0.5) as f32,
            hsl_index as f32,
        ];

        // ------------------------------------------- colour balance (wheels etc.)
        // Same maths as filters.rs: [shadows rgb, midtones rgb, highlights rgb].
        let mut balance = [0.0_f64; 9];
        let shadow_lift = ((n(light, "shadows", 0.0) + n(light, "blacks", 0.0)) / 100.0 * 0.2).clamp(-0.4, 0.4);
        let highlight_gain = ((n(light, "highlights", 0.0) + n(light, "whites", 0.0)) / 100.0 * 0.2).clamp(-0.4, 0.4);
        for channel in 0..3 { balance[channel] += shadow_lift; }
        for channel in 6..9 { balance[channel] += highlight_gain; }
        let temperature = (n(color, "temperature", 0.0) / 100.0).clamp(-1.0, 1.0);
        for band in [0usize, 3, 6] {
            balance[band] += temperature;
            balance[band + 2] -= temperature;
        }
        for (key, band) in [("shadows", 0usize), ("tint", 3usize), ("highlights", 6usize)] {
            if let Some(wheel) = colors["grading"][key].as_object() {
                let amount = wheel.get("amount").and_then(Value::as_f64).unwrap_or(0.0) / 100.0 * 0.3;
                let angle = wheel.get("hue").and_then(Value::as_f64).unwrap_or(0.0).to_radians();
                if amount.abs() > 0.001 {
                    balance[band] += amount * angle.cos();
                    balance[band + 1] += amount * (angle - std::f64::consts::TAU / 3.0).cos();
                    balance[band + 2] += amount * (angle + std::f64::consts::TAU / 3.0).cos();
                }
            }
        }
        match preset {
            "warm" => for band in [0usize, 3, 6] { balance[band] += 0.12 * preset_intensity; balance[band + 2] -= 0.10 * preset_intensity; },
            "cool" => for band in [0usize, 3, 6] { balance[band] -= 0.08 * preset_intensity; balance[band + 2] += 0.12 * preset_intensity; },
            "retro" => for band in [0usize, 3, 6] { balance[band] += 0.15 * preset_intensity; balance[band + 2] -= 0.12 * preset_intensity; },
            _ => {}
        }
        for value in &mut balance { *value = value.clamp(-1.0, 1.0); }
        u.bal_s = [balance[0] as f32, balance[1] as f32, balance[2] as f32, 0.0];
        u.bal_m = [balance[3] as f32, balance[4] as f32, balance[5] as f32, 0.0];
        u.bal_h = [balance[6] as f32, balance[7] as f32, balance[8] as f32, 0.0];

        // -------------------------------------------------------------- sharpen
        let mut sharpen = (n(detail, "sharpness", 0.0) / 100.0).clamp(0.0, 1.0);
        let mut sharpen_radius = 2.5;
        if on(effects, "sharpen") {
            let s = &effects["sharpen"];
            let radius = (n(s, "radius", 5.0).round() as i32).clamp(3, 23) | 1;
            let threshold = 1.0 - n(s, "threshold", 10.0).clamp(0.0, 100.0) / 125.0;
            sharpen += n(s, "amount", 45.0) / 100.0 * threshold;
            sharpen_radius = radius as f64 * 0.5;
        }

        // ----------------------------------------------------------------- blur
        let mut blur_mode = 0.0_f64;
        let mut blur_value = 0.0_f64;
        let mut blur_angle = 0.0_f64;
        if on(effects, "blur") {
            let b = &effects["blur"];
            // Same scaling as filters.rs (it multiplies by the preset intensity).
            let amount = n(b, "amount", 0.0) * preset_intensity.max(0.01);
            blur_angle = n(b, "angle", 0.0).to_radians();
            match b["mode"].as_str().unwrap_or("gaussian") {
                "motion" => { blur_mode = 2.0; blur_value = amount * 0.15; }
                "zoom" => { blur_mode = 3.0; blur_value = (amount / 800.0).clamp(0.0, 0.15); }
                _ => { blur_mode = 1.0; blur_value = (amount * 0.08).max(0.1); }
            }
        }
        u.fx0 = [sharpen.min(3.0) as f32, sharpen_radius as f32, blur_value as f32, blur_mode as f32];

        // ------------------------------------------------------------- vignette
        let mut vig_amount = n(detail, "vignette", 0.0) / 100.0 * 0.8;
        let (mut vig_mid, mut vig_feather, mut vig_aspect) = (0.55_f64, 0.5_f64, 1.0_f64);
        if on(effects, "vignette") {
            let v = &effects["vignette"];
            vig_amount += n(v, "amount", 0.0) / 100.0;
            vig_mid = (n(v, "midpoint", 50.0) / 100.0).clamp(0.0, 1.0);
            vig_feather = (n(v, "feather", 50.0) / 100.0).clamp(0.01, 1.0);
            vig_aspect = (1.0 + n(v, "roundness", 0.0) / 100.0 * 0.8).clamp(0.2, 2.0);
        }
        u.fx1 = [blur_angle as f32, vig_amount.clamp(-1.0, 1.0) as f32, vig_mid as f32, vig_feather as f32];

        // ---------------------------------------------------------------- grain
        let mut grain_block = 1.0_f64;
        if on(effects, "grain") {
            let g = &effects["grain"];
            grain_levels += n(g, "amount", 0.0) * n(g, "roughness", 50.0) / 50.0 * (0.6 + n(g, "size", 30.0) / 250.0);
            grain_block = (n(g, "size", 30.0) / 30.0).clamp(1.0, 4.0);
        }

        // ------------------------------------------------------------- chromatic
        let mut chroma_px = 0.0_f64;
        let mut chroma_angle = 0.0_f64;
        if on(effects, "chromatic") {
            chroma_px = n(&effects["chromatic"], "amount", 0.0) * 0.06;
            chroma_angle = n(&effects["chromatic"], "angle", 0.0).to_radians();
        }
        u.fx2 = [vig_aspect as f32, (grain_levels.clamp(0.0, 80.0) / 255.0) as f32, grain_block as f32, chroma_px as f32];

        // ----------------------------------------------------------------- glow
        let (mut glow_sigma, mut glow_brightness, mut glow_contrast) = (0.0_f64, 0.0_f64, 1.0_f64);
        if on(effects, "glow") {
            let g = &effects["glow"];
            let threshold = n(g, "threshold", 70.0) / 100.0;
            glow_sigma = (n(g, "radius", 0.0) * 0.04).max(0.1);
            glow_brightness = n(g, "amount", 0.0) / 100.0 * (0.2 - threshold * 0.08);
            glow_contrast = 1.0 + threshold * 0.12;
        }
        u.fx3 = [chroma_angle as f32, glow_sigma as f32, glow_brightness as f32, glow_contrast as f32];

        // ------------------------------------------------- pixelate / posterize
        let mut pixelate = 0.0_f64;
        if on(effects, "pixelate") {
            let p = &effects["pixelate"];
            pixelate = (n(p, "size", 12.0) * n(p, "blend", 100.0) / 100.0).clamp(1.0, 80.0);
        }
        let (mut post_levels, mut post_blend) = (0.0_f64, 1.0_f64);
        if on(effects, "posterize") {
            let p = &effects["posterize"];
            let blend = n(p, "blend", 100.0).clamp(0.0, 100.0) / 100.0;
            if blend > 0.0 {
                post_levels = n(p, "levels", 6.0).clamp(2.0, 16.0);
                post_blend = blend;
            }
        }
        u.fx4 = [pixelate as f32, post_levels as f32, post_blend as f32, 0.0];

        // ------------------------------------------------ lens / fisheye / scan
        let (mut lens_k, mut lens_zoom) = (0.0_f64, 1.0_f64);
        for key in ["lens", "fisheye"] {
            if on(effects, key) {
                let fallback = if key == "lens" { 55.0 } else { 40.0 };
                let e = &effects[key];
                lens_k += n(e, "amount", fallback) / 100.0 * 0.22 * n(e, "blend", 100.0) / 100.0;
                lens_zoom *= (n(e, "scale", 100.0) / 100.0).clamp(0.5, 1.5);
            }
        }
        let (mut scan_spacing, mut scan_amount) = (0.0_f64, 0.0_f64);
        if on(effects, "scanlines") {
            let s = &effects["scanlines"];
            scan_spacing = n(s, "spacing", 4.0).max(1.0);
            scan_amount = (n(s, "amount", 32.0) / 100.0).clamp(0.0, 1.0);
        }
        u.fx5 = [lens_k as f32, lens_zoom as f32, scan_spacing as f32, scan_amount as f32];

        // ------------------------------------------------- glitch / halftone
        let (mut glitch_px, mut glitch_speed) = (0.0_f64, 1.0_f64);
        if on(effects, "glitch") {
            let g = &effects["glitch"];
            glitch_speed = (n(g, "speed", 50.0) / 50.0).max(0.1);
            glitch_px = n(g, "amount", 0.0) * 0.1 * (0.4 + n(g, "blocks", 25.0) / 40.0) * glitch_speed;
        }
        let (mut half_size, mut half_contrast, mut half_angle) = (0.0_f64, 0.5_f64, 0.0_f64);
        if on(effects, "halftone") {
            let h = &effects["halftone"];
            half_size = n(h, "size", 5.0).clamp(1.0, 24.0) * 2.0;
            half_angle = n(h, "angle", 45.0).to_radians();
            half_contrast = n(h, "contrast", 48.0).clamp(0.0, 100.0) / 100.0;
        }
        u.fx6 = [glitch_px as f32, glitch_speed as f32, half_size as f32, half_contrast as f32];

        // ------------------------------------------------- film burn / light leak
        let (mut burn_amount, mut burn_warmth, mut burn_spread, mut burn_flicker) = (0.0_f64, 0.0_f64, 0.5_f64, 0.0_f64);
        if on(effects, "filmBurn") {
            let b = &effects["filmBurn"];
            burn_amount = n(b, "amount", 42.0) / 100.0 * 0.34;
            burn_warmth = n(b, "warmth", 0.0) / 100.0;
            burn_spread = (0.12 + n(b, "edge", 38.0) / 100.0 * 0.72).clamp(0.12, 0.84);
            burn_flicker = n(b, "flicker", 12.0) / 100.0 * 0.45;
        }
        u.fx7 = [half_angle as f32, burn_amount as f32, burn_warmth as f32, burn_spread as f32];
        let (mut leak_amount, mut leak_warmth, mut leak_position) = (0.0_f64, 0.0_f64, 0.0_f64);
        if on(effects, "lightLeak") {
            let l = &effects["lightLeak"];
            leak_amount = n(l, "amount", 40.0) / 100.0 * 0.5;
            leak_warmth = n(l, "warmth", 70.0) / 100.0;
            leak_position = n(l, "position", 35.0) / 100.0;
        }
        u.fx8 = [burn_flicker as f32, leak_amount as f32, leak_warmth as f32, leak_position as f32];
        u
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn missing_props_are_neutral() {
        assert_eq!(LayerUniforms::from_props(&Value::Null, 0.0), LayerUniforms::identity());
    }

    #[test]
    fn transform_values_reach_the_uniforms() {
        let props = json!({"transform": {
            "scale": {"w": 50.0, "h": 200.0}, "rotation": 90.0, "opacity": 25.0,
            "position": {"x": 192.0, "y": -108.0}, "crop": {"left": 192.0, "bottom": 108.0}
        }});
        let u = LayerUniforms::from_props(&props, 0.0);
        assert_eq!(u.geo0[0], 0.5);
        assert_eq!(u.geo0[1], 2.0);
        assert!((u.geo0[2] - std::f32::consts::FRAC_PI_2).abs() < 1e-5);
        assert_eq!(u.opacity(), 0.25);
        assert!((u.geo1[0] - 0.1).abs() < 1e-6);
        assert!((u.geo1[1] + 0.1).abs() < 1e-6);
        assert!((u.crop[0] - 0.1).abs() < 1e-6);
        assert!((u.crop[3] - 0.1).abs() < 1e-6);
    }

    #[test]
    fn keyframes_interpolate_transform_uniforms_on_the_native_preview_path() {
        let props = json!({"transform": {"position": {"x": 0.0, "y": 0.0},
            "scale": {"w": 100.0, "h": 100.0}, "rotation": 0.0, "opacity": 100.0}});
        let keyframes = json!([
            {"time": 1.0, "values": {"x": 0.0, "y": 100.0, "scaleW": 100.0, "scaleH": 100.0, "rotation": 0.0, "opacity": 100.0}},
            {"time": 3.0, "values": {"x": 960.0, "y": -100.0, "scaleW": 200.0, "scaleH": 50.0, "rotation": 90.0, "opacity": 40.0}}
        ]);
        let u = LayerUniforms::from_props_with_keyframes(&props, 2.0, &keyframes);
        assert!((u.geo1[0] - 0.25).abs() < 1e-5, "position X halfway");
        assert!((u.geo1[1] - 0.0).abs() < 1e-5, "position Y halfway");
        assert!((u.geo0[0] - 1.5).abs() < 1e-5, "scale X halfway");
        assert!((u.geo0[1] - 0.75).abs() < 1e-5, "scale Y halfway");
        assert!((u.geo0[2] - std::f32::consts::FRAC_PI_4).abs() < 1e-5, "rotation halfway");
        assert!((u.opacity() - 0.70).abs() < 1e-5, "opacity halfway");
        let before = LayerUniforms::from_props_with_keyframes(&props, 0.0, &keyframes);
        let after = LayerUniforms::from_props_with_keyframes(&props, 4.0, &keyframes);
        assert_eq!(before.geo0[0], 1.0);
        assert!((after.geo0[0] - 2.0).abs() < 1e-5);
    }

    #[test]
    fn legacy_fractional_keyframe_opacity_is_read_as_a_fraction() {
        let frames = json!([{"time": 0.0, "values": {"opacity": 0.25}}]);
        let u = LayerUniforms::from_props_with_keyframes(&Value::Null, 0.0, &frames);
        assert!((u.opacity() - 0.25).abs() < 1e-5);
    }

    #[test]
    fn colour_and_effect_values_reach_the_uniforms() {
        let props = json!({
            "colors": {"light": {"exposure": 1.0, "contrast": 50.0}, "color": {"saturation": -100.0, "vibrance": 40.0},
                       "grading": {"shadows": {"hue": 0.0, "amount": 100.0}},
                       "hsl": {"selected": 3, "3": {"hue": 20.0, "saturation": 50.0, "lightness": 20.0}}},
            "effects": {"blur": {"enabled": true, "amount": 100.0, "mode": "gaussian"},
                        "pixelate": {"enabled": true, "size": 20.0, "blend": 100.0},
                        "mirror": {"enabled": true}}
        });
        let u = LayerUniforms::from_props(&props, 0.0);
        assert!((u.light[0] - 0.5).abs() < 1e-6);
        assert!((u.light[1] - 1.5).abs() < 1e-6);
        assert_eq!(u.light[2], 0.0);
        assert!((u.light[3] - 0.4).abs() < 1e-6);
        assert!(u.bal_s[0] > 0.2);
        assert_eq!(u.hsl[3], 3.0);
        assert_eq!(u.hsl[0], 20.0);
        assert_eq!(u.fx0[3], 1.0);
        assert!((u.fx0[2] - 8.0).abs() < 1e-5);
        assert_eq!(u.fx4[0], 20.0);
        assert_eq!(u.geo1[2], 1.0);
    }

    #[test]
    fn transition_and_corner_distort_values_reach_the_uniforms() {
        let props = json!({"transform": {"distort": {"tl": 10.0, "tr": 20.0, "br": 30.0, "bl": 40.0}}});
        let u = LayerUniforms::from_props_with_transition(&props, 0.0, Some(("wiperight", 0.5)));
        assert_eq!(u.distort, [10.0, 20.0, 30.0, 40.0]);
        assert_eq!(u.transition, [-1.0, 0.0, 0.5, 2.0]);
    }

    #[test]
    fn shake_moves_the_layer_over_time() {
        let props = json!({"effects": {"shake": {"enabled": true, "amount": 100.0, "frequency": 1.0, "smoothness": 0.0}}});
        let a = LayerUniforms::from_props(&props, 0.0);
        let b = LayerUniforms::from_props(&props, 0.25);
        assert_ne!(a.geo1[0], b.geo1[0]);
    }
}
