// A gradient's ramp texel at p (Skia tile modes), premultiplied: geometry and (kind, tile,
// ramp v) as QuadRenderer::Paint has them, in the space of p. Needs uRamp.
vec4 gradientAt(vec2 p, vec4 geom, vec4 params) {
    int kind = int(params.x + 0.5);
    float t;
    if (kind == 1) {
        vec2 d = geom.zw - geom.xy;
        float len2 = dot(d, d);
        t = len2 > 0.0 ? dot(p - geom.xy, d) / len2 : 0.0;
    } else if (kind == 2) {
        t = geom.z > 0.0 ? length(p - geom.xy) / geom.z : 0.0;
    } else {
        vec2 d = p - geom.xy;
        t = atan(d.y, d.x) / 6.28318530718;   // 0 at 3 o'clock, clockwise in y-down
        if (t < 0.0) t += 1.0;
    }
    int tile = int(params.y + 0.5);
    if (tile == 1) t = fract(t);
    else if (tile == 2) t = 1.0 - abs(mod(t, 2.0) - 1.0);
    else if (tile == 3 && (t < 0.0 || t > 1.0)) return vec4(0.0);
    t = clamp(t, 0.0, 1.0);
    return texture(uRamp, vec2((t * 255.0 + 0.5) / 256.0, params.z));
}

