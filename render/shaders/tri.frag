#version 450
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec4 vShadow;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    vec2 viewport; vec2 origin;
    vec4 clip;
    vec4 clipRadii;
    vec4 clipInv;
    vec4 clipInv2;
    vec4 clipOuter;
} pc;

// Coverage of a plain, pixel-aligned clip rect (framebuffer px) at a fragment centre.
float outerCoverage(vec2 d, vec4 o) {
    return clamp(min(min(d.x - o.x, o.z - d.x), min(d.y - o.y, o.w - d.y)) + 0.5, 0.0, 1.0);
}

// Signed distance to a rect (left, top, right, bottom) with per-corner radii (top-left,
// top-right, bottom-right, bottom-left). Each corner rounds within its own box, so a radius may
// pass half the rect while each side's radii fit (a strip cut from a rounded rect); otherwise
// every radius clamps to half the shorter side.
float clipSdf(vec2 p, vec4 rect, vec4 radii) {
    vec2 size = rect.zw - rect.xy;
    if (radii.x + radii.y > size.x || radii.w + radii.z > size.x || radii.x + radii.w > size.y || radii.y + radii.z > size.y)
        radii = min(radii, vec4(max(min(size.x, size.y), 0.0) * 0.5));
    vec2 e = max(rect.xy - p, p - rect.zw);
    float box = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0);
    vec2 u;
    float r;
    if (p.x < rect.x + radii.x && p.y < rect.y + radii.x) { u = p - rect.xy; r = radii.x; }
    else if (p.x > rect.z - radii.y && p.y < rect.y + radii.y) { u = vec2(rect.z - p.x, p.y - rect.y); r = radii.y; }
    else if (p.x > rect.z - radii.z && p.y > rect.w - radii.z) { u = rect.zw - p; r = radii.z; }
    else if (p.x < rect.x + radii.w && p.y > rect.w - radii.w) { u = vec2(p.x - rect.x, rect.w - p.y); r = radii.w; }
    else return box;
    vec2 v = r - u;
    return max(length(max(v, 0.0)) + min(max(v.x, v.y), 0.0) - r, box);
}

// The same rounded-rect clip as the quads (quad_body.glsl clipCoverage).
float clipCoverage() {
    if (pc.clipInv2.z < 0.5) return 1.0;
    vec2 device = gl_FragCoord.xy + pc.origin;
    vec2 p = mat2(pc.clipInv.x, pc.clipInv.y, pc.clipInv.z, pc.clipInv.w) * device + pc.clipInv2.xy;
    float d = clipSdf(p, pc.clip, pc.clipRadii);
    float px = max(length(fwidth(p)) * 0.70710678, 1e-5);
    return clamp(0.5 - d / px, 0.0, 1.0) * outerCoverage(device, pc.clipOuter);
}

// Skia's shadow falloff texture (ShadowRRectOp create_falloff_texture): 128 A8 texels of
// exp(-4 d^2) - 0.018, d = 1 - i / 127, sampled with linear filtering and clamped edges.
float falloffTexel(int i) {
    float d = 1.0 - float(i) / 127.0;
    return floor((exp(-4.0 * d * d) - 0.018) * 255.0 + 0.5) / 255.0;
}
float falloff(float u) {
    float t = u * 128.0 - 0.5;
    float i0 = floor(t);
    float f = t - i0;
    int a = int(clamp(i0, 0.0, 127.0)), b = int(clamp(i0 + 1.0, 0.0, 127.0));
    return mix(falloffTexel(a), falloffTexel(b), f);
}

void main() {
    vec4 color = vColor;
    if (vShadow.w > 1.5) {
        // SkGaussianColorFilter (GrFragmentProcessors.cpp): filter the interpolated vertex
        // alpha, then modulate the resulting grayscale by the premultiplied shadow tint.
        float factor = 1.0 - vShadow.x;
        color *= exp(-factor * factor * 4.0) - 0.018;
    } else if (vShadow.w > 0.5) {
        // GrRRectShadowGeoProc: d = length(shadowParams.xy), uv.x = shadowParams.z * (1 - d).
        float d = length(vShadow.xy);
        color *= falloff(vShadow.z * (1.0 - d));
    }
    outColor = color * clipCoverage();
}
