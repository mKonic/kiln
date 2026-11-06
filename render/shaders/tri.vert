#version 450
// Triangles with per-vertex colour (Canvas.drawVertices, mesh gradients): positions in device
// pixels, colours premultiplied.
layout(location = 0) in vec2 iPos;
layout(location = 1) in vec4 iColor;

layout(push_constant) uniform Push {
    vec2 viewport; vec2 origin;      // target size and its device origin
    vec4 clip;                       // clip rect x0, y0, x1, y1 (clip space)
    vec4 clipRadii;                  // top-left, top-right, bottom-right, bottom-left
    vec4 clipInv;                    // device -> clip space: a, b, c, d
    vec4 clipInv2;                   // e, f, enabled
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = iColor;
    gl_Position = vec4((iPos - pc.origin) / pc.viewport * 2.0 - 1.0, 0.0, 1.0);
}
