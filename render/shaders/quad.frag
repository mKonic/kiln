#version 450
layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vColor;
layout(location = 3) flat in vec4 vParams;
layout(location = 4) flat in vec2 vHalf;
layout(location = 5) flat in vec4 vClip;
layout(location = 6) flat in vec4 vClipParams;
layout(location = 7) flat in vec4 vPaint;
layout(location = 8) flat in vec4 vPaint2;

layout(set = 0, binding = 0) uniform sampler2D uAtlas;
layout(set = 0, binding = 1) uniform sampler2D uRamp;

layout(location = 0) out vec4 outColor;

const float kTwoPi = 6.28318530718;

float roundedRectSdf(vec2 p, vec2 half_, float r) {
    r = min(r, min(half_.x, half_.y));
    vec2 q = abs(p) - half_ + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float clipCoverage() {
    if (vClipParams.y < 0.5) return 1.0;
    vec2 centre = (vClip.xy + vClip.zw) * 0.5;
    vec2 half_ = max((vClip.zw - vClip.xy) * 0.5, vec2(0.0));
    float d = roundedRectSdf(gl_FragCoord.xy - centre, half_, vClipParams.x);
    return clamp(0.5 - d, 0.0, 1.0);
}

// The fill colour: flat, or a gradient sampled from its ramp row (Skia tile modes).
vec4 paint() {
    int kind = int(vPaint2.x + 0.5);
    if (kind == 0) return vColor;
    vec2 p = gl_FragCoord.xy;
    float t;
    if (kind == 1) {
        vec2 d = vPaint.zw - vPaint.xy;
        float len2 = dot(d, d);
        t = len2 > 0.0 ? dot(p - vPaint.xy, d) / len2 : 0.0;
    } else if (kind == 2) {
        t = vPaint.z > 0.0 ? length(p - vPaint.xy) / vPaint.z : 0.0;
    } else {
        vec2 d = p - vPaint.xy;
        t = atan(d.y, d.x) / kTwoPi;   // 0 at 3 o'clock, clockwise in y-down
        if (t < 0.0) t += 1.0;
    }
    int tile = int(vPaint2.y + 0.5);
    if (tile == 1) t = fract(t);
    else if (tile == 2) t = 1.0 - abs(mod(t, 2.0) - 1.0);
    else if (tile == 3 && (t < 0.0 || t > 1.0)) return vec4(0.0);
    t = clamp(t, 0.0, 1.0);
    vec4 c = texture(uRamp, vec2((t * 255.0 + 0.5) / 256.0, vPaint2.z));
    return c * vColor.a;
}

void main() {
    int mode = int(vParams.y + 0.5);
    if (mode == 0) {            // solid rounded rect, analytic coverage
        float d = roundedRectSdf(vLocal, vHalf, vParams.x);
        float coverage = clamp(0.5 - d, 0.0, 1.0);
        outColor = paint() * coverage;
    } else if (mode == 1) {     // atlas coverage (text, path masks) filled with the paint
        outColor = paint() * texture(uAtlas, vUv).a;
    } else {                    // atlas colour (emoji, images), premultiplied, alpha-modulated
        outColor = texture(uAtlas, vUv) * vColor.a;
    }
    outColor *= clipCoverage();
}
