use serde_json::Value;

fn number(value: &Value, key: &str, fallback: f64) -> f64 { value[key].as_f64().filter(|n| n.is_finite()).unwrap_or(fallback) }
fn enabled(value: &Value, key: &str) -> bool { value[key]["enabled"].as_bool().unwrap_or(false) }
fn n(value: &Value, key: &str, fallback: f64) -> f64 { number(value, key, fallback) }
pub(crate) fn video_filters(clip: &Value, width: u32, height: u32, start: f64, length: f64, transition_in: Option<(&str, f64)>, transition_out: Option<(&str, f64)>) -> (String, String, String) {
    let props = &clip["props"];
    let transform = &props["transform"];
    let effects = &props["effects"];
    let colors = &props["colors"];
    let source_start = n(clip, "sourceOffset", 0.0).max(0.0);
    let mut filters = vec![format!("trim=start={source_start:.5}:duration={length:.5}"), format!("setpts=PTS-STARTPTS+{start:.5}/TB"),
        format!("scale={width}:{height}:force_original_aspect_ratio=decrease"),
        format!("pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black@0"), "setsar=1".into(), "format=rgba".into()];

    let crop = &transform["crop"];
    let left = (n(crop, "left", 0.0) / 1920.0).clamp(0.0, 0.45);
    let right = (n(crop, "right", 0.0) / 1920.0).clamp(0.0, 0.45);
    let top = (n(crop, "top", 0.0) / 1080.0).clamp(0.0, 0.45);
    let bottom = (n(crop, "bottom", 0.0) / 1080.0).clamp(0.0, 0.45);
    if left + right > 0.0 || top + bottom > 0.0 {
        filters.push(format!("crop=iw*{:.6}:ih*{:.6}:iw*{left:.6}:ih*{top:.6}", (1.0 - left - right).max(0.1), (1.0 - top - bottom).max(0.1)));
        filters.push(format!("scale={width}:{height}:force_original_aspect_ratio=decrease"));
        filters.push(format!("pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black@0"));
    }

    let light = &colors["light"];
    let color = &colors["color"];
    let detail = &colors["detail"];
    let brightness = (n(light, "exposure", 0.0) * 0.5 + n(light, "brightness", 0.0) / 100.0 * 0.25).clamp(-1.0, 1.0);
    let contrast = (1.0 + n(light, "contrast", 0.0) / 100.0 + n(detail, "clarity", 0.0) / 180.0).clamp(0.0, 4.0);
    let saturation = (1.0 + n(color, "saturation", 0.0) / 100.0 + n(color, "vibrance", 0.0) / 150.0).clamp(0.0, 3.0);
    if brightness.abs() > 0.0001 || (contrast - 1.0).abs() > 0.0001 || (saturation - 1.0).abs() > 0.0001 {
        filters.push(format!("eq=brightness={brightness:.5}:contrast={contrast:.5}:saturation={saturation:.5}"));
    }
    let temperature = (n(color, "temperature", 0.0) / 100.0).clamp(-1.0, 1.0);
    if temperature.abs() > 0.01 {
        filters.push(format!("colorbalance=rs={temperature:.4}:bs={:.4}", -temperature));
    }
    for (key, wheel_channel) in [("shadows", "s"), ("tint", "m"), ("highlights", "h")] {
        if let Some(wheel_data) = colors["grading"][key].as_object() {
            let amount = wheel_data.get("amount").and_then(Value::as_f64).unwrap_or(0.0) / 100.0 * 0.3;
            let angle = wheel_data.get("hue").and_then(Value::as_f64).unwrap_or(0.0).to_radians();
            if amount.abs() > 0.001 {
                let r = amount * angle.cos(); let b = amount * angle.sin();
                filters.push(format!("colorbalance=r{wheel_channel}={r:.4}:b{wheel_channel}={b:.4}"));
            }
        }
    }
    let sharpness = (n(detail, "sharpness", 0.0) / 100.0).clamp(0.0, 1.0);
    if sharpness > 0.001 { filters.push(format!("unsharp=5:5:{sharpness:.4}:5:5:0")); }
    let color_vignette = n(detail, "vignette", 0.0);
    if color_vignette.abs() > 1.0 { filters.push(format!("vignette=angle={:.5}", (0.35 + color_vignette.abs() / 100.0 * 0.55).clamp(0.1, 1.2))); }

    let preset = effects["preset"]["id"].as_str().unwrap_or("none");
    let intensity = (n(&effects["preset"], "intensity", 100.0) / 100.0).clamp(0.0, 1.0);
    match preset {
        "vivid" => filters.push(format!("eq=saturation={:.3}:contrast=1.08", 1.0 + 0.7 * intensity)),
        "noir" => { filters.push(format!("hue=s={:.3}", 1.0 - intensity)); filters.push(format!("eq=contrast={:.3}:brightness={:.3}", 1.0 + 0.2 * intensity, -0.02 * intensity)); },
        "fade" => filters.push(format!("eq=contrast={:.3}:brightness={:.3}:saturation={:.3}", 1.0 - 0.16 * intensity, 0.08 * intensity, 1.0 - 0.2 * intensity)),
        "warm" => filters.push(format!("colorbalance=rs={:.3}:bs={:.3}", 0.12 * intensity, -0.1 * intensity)),
        "cool" => filters.push(format!("colorbalance=rs={:.3}:bs={:.3}", -0.08 * intensity, 0.12 * intensity)),
        "cinema" => filters.push(format!("eq=contrast={:.3}:saturation={:.3}", 1.0 + 0.15 * intensity, 1.0 - 0.15 * intensity)),
        "retro" => { filters.push(format!("colorbalance=rs={:.3}:bs={:.3}", 0.15 * intensity, -0.12 * intensity)); filters.push(format!("noise=alls={:.2}:allf=t+u", 12.0 * intensity)); },
        _ => {}
    }
    if enabled(effects, "blur") {
        let blur = &effects["blur"];
        let amount = n(blur, "amount", 0.0) * intensity.max(0.01);
        let sigma = (amount * 0.08).max(0.1);
        match blur["mode"].as_str().unwrap_or("gaussian") {
            "motion" => {
                let frames = (2.0 + amount / 14.0).clamp(2.0, 9.0) as u32;
                filters.push(format!("tmix=frames={frames}"));
                let distance = amount * 0.015;
                let angle = n(blur, "angle", 0.0).to_radians();
                filters.push(format!("rgbashift=rh={:.3}:rv={:.3}:bh={:.3}:bv={:.3}", distance * angle.cos(), distance * angle.sin(), -distance * angle.cos(), -distance * angle.sin()));
            }
            "zoom" => {
                filters.push(format!("gblur=sigma={:.3}:steps=2", sigma * 0.7));
                let zoom = (1.0 + amount / 1600.0).clamp(1.0, 1.08);
                filters.push(format!("scale=iw*{zoom:.4}:ih*{zoom:.4}"));
            }
            _ => filters.push(format!("gblur=sigma={sigma:.3}:steps=2")),
        }
    }
    if enabled(effects, "grain") {
        let grain = &effects["grain"];
        let amount = n(grain, "amount", 0.0) * n(grain, "roughness", 50.0) / 50.0 * (0.6 + n(grain, "size", 30.0) / 250.0);
        filters.push(format!("noise=alls={:.2}:allf=t+u", amount.clamp(0.0, 80.0)));
    }
    if enabled(effects, "vignette") {
        let vignette = &effects["vignette"];
        let angle = (0.12 + n(vignette, "midpoint", 50.0) / 100.0 * 1.08 + n(vignette, "amount", 0.0).abs() / 100.0 * 0.35).clamp(0.05, 1.5);
        let aspect = (1.0 + n(vignette, "roundness", 0.0) / 100.0 * 0.8).clamp(0.2, 2.0);
        let softness = (0.75 + n(vignette, "feather", 50.0) / 100.0 * 0.5).clamp(0.5, 1.5);
        filters.push(format!("vignette=angle={angle:.4}:aspect={aspect:.3}:eval=frame"));
        if (softness - 1.0).abs() > 0.01 { filters.push(format!("gblur=sigma={:.3}:steps=1", (softness - 1.0).abs() * 0.7)); }
    }
    if enabled(effects, "chromatic") {
        let a = n(&effects["chromatic"], "amount", 0.0) * 0.06;
        let angle = n(&effects["chromatic"], "angle", 0.0).to_radians();
        filters.push(format!("rgbashift=rh={:.2}:rv={:.2}:bh={:.2}:bv={:.2}", a * angle.cos(), a * angle.sin(), -a * angle.cos(), -a * angle.sin()));
    }
    if enabled(effects, "glow") {
        let radius = (n(&effects["glow"], "radius", 0.0) * 0.04).max(0.1);
        let amount = n(&effects["glow"], "amount", 0.0) / 100.0;
        let threshold = n(&effects["glow"], "threshold", 70.0) / 100.0;
        filters.push(format!("gblur=sigma={radius:.3}:steps=1"));
        filters.push(format!("eq=brightness={:.4}:contrast={:.4}", amount * (0.2 - threshold * 0.08), 1.0 + threshold * 0.12));
    }
    if enabled(effects, "glitch") {
        let speed = n(&effects["glitch"], "speed", 50.0) / 50.0;
        let a = n(&effects["glitch"], "amount", 0.0) * 0.1 * (0.4 + n(&effects["glitch"], "blocks", 25.0) / 40.0) * speed.max(0.1);
        filters.push(format!("rgbashift=rh={a:.2}:bh={:.2}", -a));
        filters.push(format!("noise=alls={:.2}:allf=t", a * 2.0));
    }
    if enabled(effects, "pixelate") {
        let size = (n(&effects["pixelate"], "size", 12.0) * n(&effects["pixelate"], "blend", 100.0) / 100.0).clamp(1.0, 80.0);
        filters.push(format!("scale={}:{}:flags=neighbor,scale={width}:{height}:flags=neighbor", (width as f64 / size).max(1.0) as u32, (height as f64 / size).max(1.0) as u32));
    }
    if enabled(effects, "sharpen") {
        let sharpen = &effects["sharpen"];
        let radius = (n(sharpen, "radius", 5.0).round() as i32).clamp(3, 23) | 1;
        let threshold = 1.0 - n(sharpen, "threshold", 10.0).clamp(0.0, 100.0) / 125.0;
        let amount = n(sharpen, "amount", 45.0) / 100.0 * threshold;
        filters.push(format!("unsharp={radius}:{radius}:{amount:.3}:5:5:0"));
    }
    for key in ["lens", "fisheye"] {
        if enabled(effects, key) {
            let fallback = if key == "lens" { 55.0 } else { 40.0 };
            let amount = n(&effects[key], "amount", fallback) / 100.0 * 0.22 * n(&effects[key], "blend", 100.0) / 100.0;
            filters.push(format!("lenscorrection=cx=0.5:cy=0.5:k1={amount:.4}:k2=0"));
            let zoom = (n(&effects[key], "scale", 100.0) / 100.0).clamp(0.5, 1.5);
            if (zoom - 1.0).abs() > 0.001 { filters.push(format!("scale=iw*{zoom:.4}:ih*{zoom:.4}")); }
        }
    }
    if enabled(effects, "scanlines") { filters.push(format!("drawgrid=width=iw:height={}:thickness=1:color=black@{:.3}", n(&effects["scanlines"], "spacing", 4.0).max(1.0), (n(&effects["scanlines"], "amount", 32.0) / 100.0).clamp(0.0, 1.0))); }
    if enabled(effects, "shake") {
        // Motion is added to the overlay coordinates below, based on this enabled value.
    }
    if enabled(effects, "filmBurn") {
        let burn = &effects["filmBurn"];
        let amount = n(burn, "amount", 42.0) / 100.0 * 0.34;
        let warmth = n(burn, "warmth", 0.0) / 100.0;
        let spread = (0.12 + n(burn, "edge", 38.0) / 100.0 * 0.72).clamp(0.12, 0.84);
        let flicker = n(burn, "flicker", 12.0) / 100.0 * 0.45;
        let edge = format!("(max(0,(abs(X-W/2)/(W/2)-{:.4})/{spread:.4})*(1+sin(T*8*PI)*{flicker:.4}))", 1.0 - spread);
        filters.push(format!("geq=r='min(255,r(X,Y)+255*{:.4}*{edge})':g='min(255,g(X,Y)+255*{:.4}*{edge})':b='min(255,b(X,Y)+255*{:.4}*{edge})'", amount * warmth, amount * 0.28, amount * 0.06));
    }
    if enabled(effects, "lightLeak") {
        let leak = &effects["lightLeak"];
        let amount = n(leak, "amount", 40.0) / 100.0 * 0.5;
        let warmth = n(leak, "warmth", 70.0) / 100.0;
        let position = n(leak, "position", 35.0) / 100.0;
        let radius = (0.14 + warmth * 0.38).clamp(0.14, 0.52);
        let hotspot = format!("max(0,1-abs(X/W-{position:.4})/{radius:.4})");
        filters.push(format!("geq=r='min(255,r(X,Y)+255*{:.4}*{hotspot})':g='min(255,g(X,Y)+255*{:.4}*{hotspot})':b='min(255,b(X,Y)+255*{:.4}*{hotspot})'", amount, amount * (0.25 + warmth * 0.2), amount * (1.0 - warmth) * 0.25));
    }
    if enabled(effects, "halftone") {
        let halftone = &effects["halftone"];
        let size = n(halftone, "size", 5.0).clamp(1.0, 24.0) * 2.0;
        let angle = n(halftone, "angle", 45.0).to_radians();
        let contrast = n(halftone, "contrast", 48.0).clamp(0.0, 100.0) / 100.0;
        let px = format!("mod(X*{:.5}+Y*{:.5}+{:.3},{size:.3})-{:.3}", angle.cos(), angle.sin(), size / 2.0, size / 2.0);
        let py = format!("mod(-X*{:.5}+Y*{:.5}+{:.3},{size:.3})-{:.3}", angle.sin(), angle.cos(), size / 2.0, size / 2.0);
        let radius = format!("(1-lum(X,Y)/255)*{:.3}*{contrast:.3}", size / 2.0);
        filters.push(format!("geq=r='if(lt(hypot({px},{py}),{radius}),0,r(X,Y))':g='if(lt(hypot({px},{py}),{radius}),0,g(X,Y))':b='if(lt(hypot({px},{py}),{radius}),0,b(X,Y))'"));
    }
    if enabled(effects, "posterize") {
        let posterize = &effects["posterize"];
        let blend = n(posterize, "blend", 100.0).clamp(0.0, 100.0) / 100.0;
        let target = n(posterize, "levels", 6.0).clamp(2.0, 16.0);
        if blend > 0.0 { filters.push(format!("posterize=levels={:.0}", (16.0 - (16.0 - target) * blend).clamp(2.0, 16.0))); }
    }
    if enabled(effects, "mirror") { filters.push("hflip".into()); }

    if let Some((kind, transition_duration)) = transition_in {
        let d = transition_duration.clamp(0.05, length.max(0.05));
        if kind == "dip" {
            filters.push(format!("fade=t=in:st={:.5}:d={:.5}", start + d / 2.0, (d / 2.0).max(0.025)));
        } else if kind == "wipe" {
            filters.push(format!("geq=a='alpha(X,Y)*if(lte(X/W,min(max((T-{start:.5})/{d:.5},0),1)),1,0)'"));
        } else if kind == "iris" {
            filters.push(format!("geq=a='alpha(X,Y)*if(lte(hypot(X-W/2,Y-H/2),min(W,H)*0.5*min(max((T-{start:.5})/{d:.5},0),1)),1,0)'"));
        } else {
            filters.push(format!("fade=t=in:st={start:.5}:d={d:.5}:alpha=1"));
        }
    }
    if let Some((kind, transition_duration)) = transition_out {
        let d = transition_duration.clamp(0.05, length.max(0.05));
        if kind == "dip" { filters.push(format!("fade=t=out:st={:.5}:d={:.5}", start + length - d, (d / 2.0).max(0.025))); }
        filters.push(format!("tpad=stop_mode=clone:stop_duration={d:.5}"));
    }

    let distort = &transform["distort"];
    let tl = n(distort, "tl", 0.0) * width as f64 / 1920.0;
    let tr = n(distort, "tr", 0.0) * width as f64 / 1920.0;
    let br = n(distort, "br", 0.0) * width as f64 / 1920.0;
    let bl = n(distort, "bl", 0.0) * width as f64 / 1920.0;
    if tl.abs() + tr.abs() + br.abs() + bl.abs() > 0.01 {
        filters.push(format!("perspective=x0={tl:.3}:y0={tl:.3}:x1={:.3}:y1={tr:.3}:x2={:.3}:y2={:.3}:x3={bl:.3}:y3={:.3}:sense=source:eval=frame", width as f64 - tr, width as f64 - br, height as f64 - br, height as f64 - bl));
    }

    let scale_w = (n(&transform["scale"], "w", 100.0) / 100.0).clamp(0.01, 10.0);
    let scale_h = (n(&transform["scale"], "h", 100.0) / 100.0).clamp(0.01, 10.0);
    if (scale_w - 1.0).abs() > 0.0001 || (scale_h - 1.0).abs() > 0.0001 { filters.push(format!("scale=iw*{scale_w:.5}:ih*{scale_h:.5}")); }
    let rotation = n(transform, "rotation", 0.0).to_radians();
    if rotation.abs() > 0.0001 { filters.push(format!("rotate={rotation:.6}:ow=rotw(iw):oh=roth(ih):c=black@0")); }
    let opacity = (n(transform, "opacity", 100.0) / 100.0).clamp(0.0, 1.0);
    if opacity < 0.999 { filters.push(format!("colorchannelmixer=aa={opacity:.4}")); }
    let x = n(&transform["position"], "x", 0.0) * width as f64 / 1920.0;
    let y = n(&transform["position"], "y", 0.0) * height as f64 / 1080.0;
    let (shake_x, shake_y) = if enabled(effects, "shake") {
        let amount = n(&effects["shake"], "amount", 18.0) * width as f64 / 1920.0 * 0.18;
        let freq = (n(&effects["shake"], "frequency", 8.0) * (1.0 - n(&effects["shake"], "smoothness", 35.0) / 120.0)).max(0.5);
        (format!("+sin(2*PI*t*{freq:.3})*{amount:.3}"), format!("+cos(2*PI*t*{freq:.3})*{amount:.3}"))
    } else { (String::new(), String::new()) };
    let overlay_x = format!("(main_w-overlay_w)/2+{x:.3}{shake_x}");
    let overlay_y = format!("(main_h-overlay_h)/2+{y:.3}{shake_y}");
    (filters.join(","), overlay_x, overlay_y)
}

pub(crate) fn audio_filters(clip: &Value, length: f64, total: f64) -> String {
    let props = &clip["props"];
    let audio = &props["audio"];
    let effects = &props["effects"];
    let mut filters = vec![format!("atrim=start={:.5}:duration={length:.5}", n(clip, "sourceOffset", 0.0).max(0.0)), "asetpts=PTS-STARTPTS".into()];
    let volume = (n(audio, "volume", 100.0) / 100.0).clamp(0.0, 2.0);
    if (volume - 1.0).abs() > 0.0001 { filters.push(format!("volume={volume:.4}")); }
    let pan = (n(audio, "pan", 0.0) / 100.0).clamp(-1.0, 1.0);
    if pan.abs() > 0.001 { filters.push(format!("stereotools=balance_out={pan:.4}")); }
    let bass = n(audio, "bass", 0.0); let treble = n(audio, "treble", 0.0);
    if bass.abs() > 0.001 { filters.push(format!("equalizer=f=100:t=q:w=1:g={bass:.3}")); }
    if treble.abs() > 0.001 { filters.push(format!("equalizer=f=4000:t=q:w=1:g={treble:.3}")); }
    if enabled(effects, "eq") {
        for (key, freq) in [("low", 100), ("mid", 1000), ("high", 8000)] {
            let gain = n(&effects["eq"], key, 0.0);
            if gain.abs() > 0.001 { filters.push(format!("equalizer=f={freq}:t=q:w=1:g={gain:.3}")); }
        }
    }
    if enabled(effects, "reverb") {
        let wet = (n(&effects["reverb"], "wet", 0.0) / 100.0).clamp(0.0, 1.0);
        let delay = (80.0 + n(&effects["reverb"], "room", 0.0) * 3.0).round() as u32;
        let damping = 1.0 - n(&effects["reverb"], "damp", 50.0).clamp(0.0, 100.0) / 125.0;
        filters.push(format!("aecho=1.0:{:.3}:{delay}|{}:{:.3}|{:.3}", 1.0 - wet * 0.45, delay * 2, wet * 0.35 * damping, wet * 0.18 * damping));
    }
    if enabled(effects, "delay") {
        let delay = n(&effects["delay"], "time", 320.0).clamp(1.0, 2000.0).round() as u32;
        let feedback = (n(&effects["delay"], "feedback", 0.0) / 100.0).clamp(0.0, 0.95);
        let mix = (n(&effects["delay"], "mix", 0.0) / 100.0).clamp(0.0, 1.0);
        filters.push(format!("aecho=1.0:{:.3}:{delay}|{}:{feedback:.3}|{:.3}", 1.0 - mix * 0.5, delay * 2, feedback * 0.5));
    }
    if enabled(effects, "compressor") { filters.push(format!("acompressor=threshold={:.3}dB:ratio={:.3}:attack={:.3}:release={:.3}", n(&effects["compressor"], "threshold", -18.0), n(&effects["compressor"], "ratio", 4.0), n(&effects["compressor"], "attack", 12.0), n(&effects["compressor"], "release", 140.0))); }
    if enabled(effects, "limiter") { filters.push(format!("alimiter=limit={:.5}:release={:.3}", 10_f64.powf(n(&effects["limiter"], "ceiling", -1.0) / 20.0), n(&effects["limiter"], "release", 80.0))); }
    if enabled(effects, "denoise") {
        let reduction = n(&effects["denoise"], "reduction", 0.0);
        let sensitivity = n(&effects["denoise"], "sensitivity", 50.0).clamp(0.0, 100.0);
        filters.push(format!("afftdn=nr={:.1}:nf={:.1}", (reduction * 0.24).clamp(0.0, 24.0), -50.0 + sensitivity * 0.3));
    }
    if audio["fadeIn"].as_bool().unwrap_or(false) { filters.push(format!("afade=t=in:st=0:d={:.4}", n(audio, "fadeInDuration", 1.0).clamp(0.0, length))); }
    if audio["fadeOut"].as_bool().unwrap_or(false) { let d = n(audio, "fadeOutDuration", 1.0).clamp(0.0, length); filters.push(format!("afade=t=out:st={:.4}:d={d:.4}", (length - d).max(0.0))); }
    let start = n(clip, "start", 0.0).max(0.0);
    filters.push(format!("adelay={}:all=1", (start * 1000.0).round() as u64));
    let _ = total;
    filters.join(",")
}

