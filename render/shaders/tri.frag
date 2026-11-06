#version 450
layout(location = 0) in vec4 vColor;
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

void main() { outColor = vColor * clipCoverage(); }
