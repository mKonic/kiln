#version 450
// Triangles with per-vertex colour (Canvas.drawVertices, mesh gradients, shadows): positions in
// device pixels, colours premultiplied.
layout(location = 0) in vec2 iPos;
layout(location = 1) in vec4 iColor;
layout(location = 2) in vec4 iShadow;   // shadow params (x, y, z) or texture coordinates (x, y), and mode (w)
layout(location = 3) in vec4 iPaint;    // the paint shader's geometry (texture-coordinate space)
layout(location = 4) in vec4 iPaint2;   // shader kind (0 none, 1-3 gradients, 4 image), tile x, ramp v, tile y
layout(location = 5) in vec4 iPaint3;   // filter row + 1, blend mode + 1, image size

layout(push_constant) uniform Push {
    vec2 viewport; vec2 origin;      // target size and its device origin
    vec4 clip;                       // clip rect x0, y0, x1, y1 (clip space)
    vec4 clipRadii;                  // top-left, top-right, bottom-right, bottom-left
    vec4 clipInv;                    // device -> clip space: a, b, c, d
    vec4 clipInv2;                   // e, f, enabled
} pc;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec4 vShadow;
layout(location = 2) flat out vec4 vPaint;
layout(location = 3) flat out vec4 vPaint2;
layout(location = 4) flat out vec4 vPaint3;

void main() {
    vColor = iColor;
    vShadow = iShadow;
    vPaint = iPaint;
    vPaint2 = iPaint2;
    vPaint3 = iPaint3;
    gl_Position = vec4((iPos - pc.origin) / pc.viewport * 2.0 - 1.0, 0.0, 1.0);
}
