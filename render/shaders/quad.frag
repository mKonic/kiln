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
layout(set = 0, binding = 2) uniform sampler2D uFilters;

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

// --- blend modes (Skia / W3C compositing, premultiplied src over dst) -----------------------

float lum(vec3 c) { return dot(c, vec3(0.3, 0.59, 0.11)); }
vec3 clipColor(vec3 c) {
    float l = lum(c), n = min(min(c.r, c.g), c.b), x = max(max(c.r, c.g), c.b);
    if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-6);
    if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-6);
    return c;
}
vec3 setLum(vec3 c, float l) { return clipColor(c + (l - lum(c))); }
float sat(vec3 c) { return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b); }
vec3 setSat(vec3 c, float s) {
    float mn = min(min(c.r, c.g), c.b), mx = max(max(c.r, c.g), c.b);
    return mx > mn ? (c - mn) * s / (mx - mn) : vec3(0.0);
}
float dodge(float s, float d) { return d == 0.0 ? 0.0 : (s >= 1.0 ? 1.0 : min(1.0, d / (1.0 - s))); }
float burn(float s, float d) { return d >= 1.0 ? 1.0 : (s <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - d) / s)); }
float hardl(float s, float d) { return s <= 0.5 ? 2.0 * s * d : 1.0 - 2.0 * (1.0 - s) * (1.0 - d); }
float softl(float s, float d) {
    if (s <= 0.5) return d - (1.0 - 2.0 * s) * d * (1.0 - d);
    float g = d <= 0.25 ? ((16.0 * d - 12.0) * d + 4.0) * d : sqrt(d);
    return d + (2.0 * s - 1.0) * (g - d);
}

vec4 blend(int m, vec4 s, vec4 d) {
    if (m == 0) return vec4(0.0);
    if (m == 1) return s;
    if (m == 2) return d;
    if (m == 3) return s + d * (1.0 - s.a);
    if (m == 4) return d + s * (1.0 - d.a);
    if (m == 5) return s * d.a;
    if (m == 6) return d * s.a;
    if (m == 7) return s * (1.0 - d.a);
    if (m == 8) return d * (1.0 - s.a);
    if (m == 9) return s * d.a + d * (1.0 - s.a);
    if (m == 10) return d * s.a + s * (1.0 - d.a);
    if (m == 11) return s * (1.0 - d.a) + d * (1.0 - s.a);
    if (m == 12) return min(s + d, vec4(1.0));
    if (m == 13) return s * d;
    if (m == 14) return s + d - s * d;
    // Separable and non-separable modes on unpremultiplied colour, composited source-over.
    vec3 sc = s.a > 0.0 ? s.rgb / s.a : vec3(0.0), dc = d.a > 0.0 ? d.rgb / d.a : vec3(0.0);
    vec3 b;
    if (m == 15) b = vec3(hardl(dc.r, sc.r), hardl(dc.g, sc.g), hardl(dc.b, sc.b));          // overlay
    else if (m == 16) b = min(sc, dc);
    else if (m == 17) b = max(sc, dc);
    else if (m == 18) b = vec3(dodge(sc.r, dc.r), dodge(sc.g, dc.g), dodge(sc.b, dc.b));
    else if (m == 19) b = vec3(burn(sc.r, dc.r), burn(sc.g, dc.g), burn(sc.b, dc.b));
    else if (m == 20) b = vec3(hardl(sc.r, dc.r), hardl(sc.g, dc.g), hardl(sc.b, dc.b));
    else if (m == 21) b = vec3(softl(sc.r, dc.r), softl(sc.g, dc.g), softl(sc.b, dc.b));
    else if (m == 22) b = abs(sc - dc);
    else if (m == 23) b = sc + dc - 2.0 * sc * dc;
    else if (m == 24) b = sc * dc;
    else if (m == 25) b = setLum(setSat(sc, sat(dc)), lum(dc));
    else if (m == 26) b = setLum(setSat(dc, sat(sc)), lum(dc));
    else if (m == 27) b = setLum(sc, lum(dc));
    else b = setLum(dc, lum(sc));
    float a = s.a + d.a - s.a * d.a;
    vec3 rgb = (1.0 - d.a) * s.rgb + (1.0 - s.a) * d.rgb + s.a * d.a * b;
    return vec4(rgb, a);
}

// Colour filter from the table row: matrix on unpremultiplied colour, or the tint colour blended
// (as source) onto the pixel (as destination).
vec4 applyFilter(vec4 c) {
    int row = int(vParams.z + 0.5);
    if (vParams.z < -0.5) return c;
    vec4 meta = texelFetch(uFilters, ivec2(5, row), 0);
    if (meta.x > 1.5) return blend(int(meta.y + 0.5), texelFetch(uFilters, ivec2(4, row), 0), c);
    vec4 u = c.a > 0.0 ? vec4(c.rgb / c.a, c.a) : vec4(0.0);
    vec4 r = vec4(dot(texelFetch(uFilters, ivec2(0, row), 0), u), dot(texelFetch(uFilters, ivec2(1, row), 0), u),
                  dot(texelFetch(uFilters, ivec2(2, row), 0), u), dot(texelFetch(uFilters, ivec2(3, row), 0), u));
    r = clamp(r + texelFetch(uFilters, ivec2(4, row), 0), 0.0, 1.0);
    return vec4(r.rgb * r.a, r.a);
}

void main() {
    int mode = int(vParams.y + 0.5);
    // Skia order: source colour (paint or image texel times alpha), colour filter, coverage.
    if (mode == 0) {            // solid rounded rect, analytic coverage
        float d = roundedRectSdf(vLocal, vHalf, vParams.x);
        outColor = applyFilter(paint()) * clamp(0.5 - d, 0.0, 1.0);
    } else if (mode == 1) {     // atlas coverage (text, path masks) filled with the paint
        outColor = applyFilter(paint()) * texture(uAtlas, vUv).a;
    } else {                    // atlas colour (emoji, images), premultiplied, alpha-modulated
        outColor = applyFilter(texture(uAtlas, vUv) * vColor.a);
    }
    outColor *= clipCoverage();
}
