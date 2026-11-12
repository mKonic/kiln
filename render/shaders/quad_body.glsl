// Shared body of quad.frag and quad_filtered.frag.
layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vColor;
layout(location = 3) flat in vec4 vParams;
layout(location = 4) flat in vec2 vHalf;
layout(location = 5) flat in vec4 vClip;
layout(location = 6) flat in vec4 vClipParams;
layout(location = 7) flat in vec4 vPaint;
layout(location = 8) flat in vec4 vPaint2;
layout(location = 9) flat in vec4 vClipInv;
layout(location = 10) flat in vec4 vClipInv2;
layout(location = 11) flat in vec2 vUvMin;
layout(location = 12) flat in vec2 vUvMax;
layout(location = 13) flat in vec4 vClipOuter;

layout(set = 0, binding = 0) uniform sampler2D uAtlas;
layout(set = 0, binding = 1) uniform sampler2D uRamp;
layout(set = 0, binding = 2) uniform sampler2D uFilters;
layout(set = 0, binding = 3) uniform sampler2D uDst;   // blend modes: the target's content so far

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push { vec2 viewport; vec2 origin; } pc;
// Device-space position of this fragment (layers render a window of device space).
vec2 fragDevice() { return gl_FragCoord.xy + pc.origin; }

const float kTwoPi = 6.28318530718;

float roundedRectSdf(vec2 p, vec2 half_, float r) {
    r = min(r, min(half_.x, half_.y));
    vec2 q = abs(p) - half_ + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Coverage of a signed distance in a space whose units may not be device pixels.
float aaCoverage(float d, vec2 p) {
    float px = max(length(fwidth(p)) * 0.70710678, 1e-5);
    return clamp(0.5 - d / px, 0.0, 1.0);
}

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

float clipCoverage() {
    if (vClipInv2.z < 0.5) return 1.0;
    vec2 p = mat2(vClipInv.x, vClipInv.y, vClipInv.z, vClipInv.w) * fragDevice() + vClipInv2.xy;
    return aaCoverage(clipSdf(p, vClip, vClipParams), p) * outerCoverage(fragDevice(), vClipOuter);
}

// A gradient's ramp texel at this fragment (Skia tile modes), premultiplied; kind 0 has none.
vec4 paintRaw() {
    int kind = int(vPaint2.x + 0.5);
    vec2 p = fragDevice();
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
    return texture(uRamp, vec2((t * 255.0 + 0.5) / 256.0, vPaint2.z));
}

// The fill colour: flat, or the gradient.
// Skia's A8 text mask gamma (SkMaskGamma.cpp, SkTMaskGamma_build_correcting_lut, contrast 0):
// the coverage that makes the colour, blended in gamma space, land where blending it in a
// linear space of exponent `g` would. The table follows the colour's luminance
// (SkComputeLuminance, quantised to 3 bits); `m` was rasterised for white with the same g, so
// it is first taken back to the outline's coverage. g = 1: m unchanged.
float textCoverage(float m, vec4 color, float g) {
    if (g == 1.0 || m <= 0.0 || m >= 1.0) return m;
    vec3 rgb = color.a > 0.0 ? color.rgb / color.a : vec3(0.0);
    float lum = floor(dot(floor(rgb * 255.0 + 0.5), vec3(54.0, 183.0, 19.0)) / 256.0);
    float q = floor(lum / 32.0);                                   // 3 bits, scaled back to 0..255
    float src = (q * 32.0 + q * 4.0 + floor(q / 2.0)) / 255.0;
    float dst = 1.0 - src;
    float a = pow(m, g);                                           // the white table, undone
    if (abs(src - dst) < 1.0 / 256.0) return a;
    float linOut = pow(src, g) * a + (1.0 - a) * pow(dst, g);
    return clamp((pow(linOut, 1.0 / g) - dst) / (src - dst), 0.0, 1.0);
}

vec4 paint() {
    if (int(vPaint2.x + 0.5) == 0) return vColor;
    return paintRaw() * vColor.a;
}

#ifdef KILN_FILTERS
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

#else
// The common pipeline has no colour filters: their code costs every pixel on some GPUs (and on
// SwiftShader) even when unused, so filtered quads use the KILN_FILTERS variant.
vec4 applyFilter(vec4 c) { return c; }
#endif

// --- press ripple: RippleShader (frameworks/base RippleShader.java, android-16.0.0_r2) -------
// Uniforms: in_origin = vUvMin, in_touch = vUvMax, in_color = vColor (straight), in_progress,
// in_maxRadius and the resolution in vPaint, in_noisePhase, in_sparkleColor.a and the paint's
// alpha in vPaint2,
// in_tCircle1..3 / in_tRotation1..3 in filter table row vParams.x. The sparkle colour is white.

float rippleTriangleNoise(vec2 n) {
    n = fract(n * vec2(5.3987, 5.4421));
    n += dot(n.yx, n.xy + vec2(21.5351, 14.3137));
    float xy = n.x * n.y;
    return fract(xy * 95.4307) + fract(xy * 75.04961) - 1.0;
}
const float kRipplePi = 3.1415926535897932384626;
float rippleThreshold(float v, float l, float h) { return step(l, v) * (1.0 - step(h, v)); }
float rippleSparkles(vec2 uv, float t, float sparkleAlpha) {
    float n = rippleTriangleNoise(uv);
    float s = 0.0;
    for (float i = 0.0; i < 4.0; i += 1.0) {
        float l = i * 0.1;
        float h = l + 0.05;
        float o = sin(kRipplePi * (t + 0.35 * i));
        s += rippleThreshold(n + o, l, h);
    }
    return clamp(s, 0.0, 1.0) * sparkleAlpha;
}
float rippleSoftCircle(vec2 uv, vec2 xy, float radius, float blur) {
    float blurHalf = blur * 0.5;
    float d = distance(uv, xy);
    return 1.0 - smoothstep(1.0 - blurHalf, 1.0 + blurHalf, d / radius);
}
float rippleSoftRing(vec2 uv, vec2 xy, float radius, float progress, float blur) {
    float thickness = 0.05 * radius;
    float currentRadius = radius * progress;
    float circleOuter = rippleSoftCircle(uv, xy, currentRadius + thickness, blur);
    float circleInner = rippleSoftCircle(uv, xy, max(currentRadius - thickness, 0.0), blur);
    return clamp(circleOuter - circleInner, 0.0, 1.0);
}
float rippleSubProgress(float start, float end, float progress) {
    float sub = clamp(progress, start, end);
    return (sub - start) / (end - start);
}
mat2 rippleRotate2d(vec2 rad) { return mat2(rad.x, -rad.y, rad.y, rad.x); }
float rippleCircleGrid(vec2 resolution, vec2 coord, vec2 center, vec2 rotation, float cellDiameter) {
    coord = rippleRotate2d(rotation) * (center - coord) + center;
    coord = mod(coord, cellDiameter) / resolution;
    float normalRadius = cellDiameter / resolution.y * 0.5;
    float radius = 0.65 * normalRadius;
    return rippleSoftCircle(coord, vec2(normalRadius), radius, radius * 50.0);
}
float rippleTurbulence(vec2 uv, vec4 t0, vec4 t1, vec4 t2) {
    const vec2 scale = vec2(0.8);
    uv = uv * scale;
    float g1 = rippleCircleGrid(scale, uv, t0.xy, t1.zw, 0.17);
    float g2 = rippleCircleGrid(scale, uv, t0.zw, t2.xy, 0.2);
    float g3 = rippleCircleGrid(scale, uv, t1.xy, t2.zw, 0.275);
    float v = (g1 * g1 + g2 - g3) * 0.5;
    return clamp(0.45 + 0.8 * v, 0.0, 1.0);
}
vec4 rippleColor(vec2 p) {
    int row = int(vParams.x + 0.5);
    vec4 t0 = texelFetch(uFilters, ivec2(0, row), 0);   // tCircle1, tCircle2
    vec4 t1 = texelFetch(uFilters, ivec2(1, row), 0);   // tCircle3, tRotation1
    vec4 t2 = texelFetch(uFilters, ivec2(2, row), 0);   // tRotation2, tRotation3
    float progress = vPaint.x, maxRadius = vPaint.y;
    vec2 resolutionScale = 1.0 / vPaint.zw, noiseScale = 2.1 / vPaint.zw;
    float noisePhase = vPaint2.x, sparkleA = vPaint2.y;
    vec2 origin = vUvMin, touch = vUvMax;

    float fadeIn = rippleSubProgress(0.0, 0.13, progress);
    float scaleIn = rippleSubProgress(0.0, 1.0, progress);
    float fadeOutNoise = rippleSubProgress(0.4, 0.5, progress);
    float fadeOutRipple = rippleSubProgress(0.4, 1.0, progress);
    vec2 center = mix(touch, origin, clamp(progress * 2.0, 0.0, 1.0));
    float ring = rippleSoftRing(p, center, maxRadius, scaleIn, 1.0);
    float alpha = min(fadeIn, 1.0 - fadeOutNoise);
    vec2 uv = p * resolutionScale;
    vec2 densityUv = uv - mod(uv, noiseScale);
    float turb = rippleTurbulence(uv, t0, t1, t2);
    float sparkleAlpha = rippleSparkles(densityUv, noisePhase, sparkleA) * ring * alpha * turb;
    float fade = min(fadeIn, 1.0 - fadeOutRipple);
    float waveAlpha = rippleSoftCircle(p, center, maxRadius * scaleIn, 1.0) * fade * vColor.a;
    vec4 waveColor = vec4(vColor.rgb * waveAlpha, waveAlpha);
    vec4 sparkleColor = vec4(vec3(sparkleA), sparkleA);
    return mix(waveColor, sparkleColor, sparkleAlpha) * vPaint2.z;   // times the paint alpha
}

// --- stretch overscroll (HWUI StretchEffect.cpp, uInterpolationStrength 0.7) ----------------

float stretchAxis(float inPos, float overscroll, float dim) {
    float distStretched = dim / (1.0 + abs(overscroll));
    float invDist = 1.0 / dim;
    float diff = distStretched - dim;
    if (overscroll > 0.0) {
        if (inPos <= dim) {
            float offsetPos = dim - inPos;
            float posVar = mix(1.0, offsetPos * invDist, 0.7);
            return distStretched - (offsetPos / (1.0 + overscroll * posVar));
        }
        return diff + inPos;
    } else if (overscroll < 0.0) {
        float affected = dim - dim;   // viewportDimension - uStretchAffectedDist = 0
        if (inPos >= affected) {
            float offsetPos = inPos - affected;
            float posVar = mix(1.0, offsetPos * invDist, 0.7);
            return dim - (distStretched - (offsetPos / (1.0 + (-overscroll) * posVar)));
        }
        return -diff + inPos;
    }
    return inPos;
}

// --- blur (one pass of a separable Gaussian, Skia GrGaussianConvolutionFragmentProcessor) ---

// A layer texel at uv: edge texels repeat (clamp) or nothing (decal) outside the layer.
vec4 blurTap(vec2 uv, vec2 lo, vec2 hi, bool decal) {
    if (decal) {
        if (any(lessThan(uv, vUvMin)) || any(greaterThan(uv, vUvMax))) return vec4(0.0);
        return texture(uAtlas, clamp(uv, lo, hi));
    }
    return texture(uAtlas, clamp(uv, lo, hi));
}

vec4 blurPass() {
    vec2 dir = vPaint.xy;   // one texel along the pass axis, in uv
    float sigma = vPaint.z;
    bool decal = vPaint.w > 0.5;
    vec2 lo = vUvMin + 0.5 * abs(dir), hi = vUvMax - 0.5 * abs(dir);
    int n = min(int(ceil(3.0 * sigma)), 192);
    float k = -0.5 / (sigma * sigma);
    vec4 sum = blurTap(vUv, lo, hi, decal);
    float wsum = 1.0;
    // Pairs of taps share one bilinear fetch at their weighted centre.
    for (int i = 1; i <= n; i += 2) {
        float f0 = float(i), f1 = float(i + 1);
        float w0 = exp(f0 * f0 * k), w1 = i + 1 <= n ? exp(f1 * f1 * k) : 0.0;
        float w = w0 + w1;
        float o = (f0 * w0 + f1 * w1) / w;
        sum += (blurTap(vUv + dir * o, lo, hi, decal) + blurTap(vUv - dir * o, lo, hi, decal)) * w;
        wsum += 2.0 * w;
    }
    return sum / wsum;
}

// --- progressive blur (ui-graphics BlurShaders.kt blurCoreSkSl) -------------------------------
// One pass whose radius varies per pixel: the gradient paint's alpha is the intensity, times the
// maximum radius (vColor.z); sigma is radius / 2. Clamp renormalises over in-bounds taps, decal
// keeps the full weight (edges fade).
float gaussian(float x, float sigma) { return exp(-(x * x) / (2.0 * sigma * sigma)); }

vec4 progressiveBlurPass() {
    vec2 dir = vColor.xy;   // one texel along the pass axis, in uv
    float radius = vColor.z * clamp(paintRaw().a, 0.0, 1.0);
    float unbounded = vParams.x > 0.5 ? 1.0 : 0.0;
    vec2 axis = abs(dir.x) > abs(dir.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    float lo = dot(vUvMin, axis), hi = dot(vUvMax, axis);
    vec4 result = texture(uAtlas, vUv);
    float r = floor(radius);
    if (r < 1.0) return result;
    float sigma = max(radius / 2.0, 1.0);
    float weightSum = 1.0;
    for (float i = 1.0; i < 150.0; i += 2.0) {
        if (i >= r) break;
        float wl = gaussian(i, sigma), wh = gaussian(i + 1.0, sigma), w = wl + wh;
        vec2 off = dir * (i + wh / w);
        for (int side = 0; side < 2; ++side) {
            vec2 uv = side == 0 ? vUv - off : vUv + off;
            float a = dot(uv, axis);
            float m = step(lo, a) * (1.0 - step(hi, a));
            weightSum += w * max(m, unbounded);
            if (m > 0.0) result += w * texture(uAtlas, uv);
        }
    }
    float ow = gaussian(r, sigma) * mod(r, 2.0) * (1.0 - step(150.0, r));
    for (int side = 0; side < 2; ++side) {
        vec2 uv = side == 0 ? vUv - dir * r : vUv + dir * r;
        float a = dot(uv, axis);
        float m = step(lo, a) * (1.0 - step(hi, a));
        weightSum += ow * max(m, unbounded);
        if (m > 0.0) result += ow * texture(uAtlas, uv);
    }
    return result / weightSum;
}

void main() {
    int mode = int(vParams.y + 0.5);
    if (mode == 5) {
        outColor = progressiveBlurPass() * clipCoverage();
        return;
    }
    if (mode == 4) {
        outColor = blurPass() * vColor.a * clipCoverage();
        return;
    }
    if (mode == 3) {            // a layer seen through the stretch overscroll
        vec2 size = vPaint.zw;
        vec2 uvMin = vec2(0.0);
        vec2 local = (vLocal + vHalf);   // pixels within the layer rect
        vec2 src = vec2(stretchAxis(local.x, vPaint.x, size.x), stretchAxis(local.y, vPaint.y, size.y));
        vec2 uvSize = vUvMax - vUvMin;
        outColor = texture(uAtlas, vUvMin + clamp(src / size, 0.0, 1.0) * uvSize) * vColor.a;
        outColor *= clipCoverage();
        return;
    }
    // Skia order: source colour (paint or image texel times alpha), colour filter, coverage.
    vec4 src;
    float coverage;
    if (mode == 7) {            // press ripple: the shader over HWUI's anti-aliased circle
        src = rippleColor(vUvMin + vLocal);
        coverage = aaCoverage(length(vLocal) - vHalf.x, vLocal);
    } else if (mode == 6) {     // bitmap shader alpha and independent geometry coverage
        vec2 mask = texture(uAtlas, vUv).rg;
        src = applyFilter(paint() * mask.r);
        coverage = mask.g;
    } else if (mode == 0) {     // solid rounded rect, analytic coverage
        src = applyFilter(paint());
        coverage = aaCoverage(roundedRectSdf(vLocal, vHalf, vParams.x), vLocal);
    } else if (mode == 1) {     // atlas coverage (text, path masks) filled with the paint
        src = applyFilter(paint());
        coverage = texture(uAtlas, vUv).a;
    } else if (mode == 8) {     // glyph coverage with the text colour's mask gamma
        src = applyFilter(paint());
        coverage = textCoverage(texture(uAtlas, vUv).a, vColor, vParams.x);
    } else {                    // atlas colour (emoji, images), premultiplied, alpha-modulated
        vec2 uv = vUv;
        if (vParams.x > 0.5) {  // FilterQuality.None: the nearest texel
            vec2 texels = vec2(textureSize(uAtlas, 0));
            uv = (floor(uv * texels) + 0.5) / texels;
        }
        src = applyFilter(texture(uAtlas, uv) * vColor.a);
        coverage = 1.0;
    }
    coverage *= clipCoverage();
#ifdef KILN_FILTERS
    // A blend mode: the result replaces the pixel, lerped to the destination by coverage (Skia).
    int blendMode = int(vParams.w + 0.5) - 1;
    if (blendMode >= 0) {
        vec4 dst = texture(uDst, gl_FragCoord.xy / vec2(textureSize(uDst, 0)));
        outColor = mix(dst, blend(blendMode, src, dst), coverage);
        return;
    }
#endif
    outColor = src * coverage;
}
