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
} pc;

float roundedRectSdf(vec2 p, vec2 half_, float r) {
    r = min(r, min(half_.x, half_.y));
    vec2 q = abs(p) - half_ + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// The same rounded-rect clip as the quads (quad_body.glsl clipCoverage).
float clipCoverage() {
    if (pc.clipInv2.z < 0.5) return 1.0;
    vec2 device = gl_FragCoord.xy + pc.origin;
    vec2 p = mat2(pc.clipInv.x, pc.clipInv.y, pc.clipInv.z, pc.clipInv.w) * device + pc.clipInv2.xy;
    vec2 centre = (pc.clip.xy + pc.clip.zw) * 0.5;
    vec2 half_ = max((pc.clip.zw - pc.clip.xy) * 0.5, vec2(0.0));
    vec2 q = p - centre;
    float r = q.x < 0.0 ? (q.y < 0.0 ? pc.clipRadii.x : pc.clipRadii.w) : (q.y < 0.0 ? pc.clipRadii.y : pc.clipRadii.z);
    float d = roundedRectSdf(q, half_, r);
    float px = max(length(fwidth(p)) * 0.70710678, 1e-5);
    return clamp(0.5 - d / px, 0.0, 1.0);
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
