#version 450
// One instanced unit quad per primitive. Coordinates are framebuffer pixels, +y down.
layout(location = 0) in vec4 iRect;    // x, y, w, h
layout(location = 1) in vec4 iUv;      // u0, v0, u1, v1 (atlas, normalised)
layout(location = 2) in vec4 iColor;   // premultiplied rgba
layout(location = 3) in vec4 iParams;  // cornerRadius, mode, unused, unused

layout(push_constant) uniform Push { vec2 viewport; } pc;

layout(location = 0) out vec2 vLocal;      // pixels from the rect centre
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vColor;
layout(location = 3) flat out vec4 vParams;
layout(location = 4) flat out vec2 vHalf;

void main() {
    vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    // Grow solid shapes by one pixel so the anti-aliased edge has room.
    float grow = iParams.y == 0.0 ? 1.0 : 0.0;
    vec2 pos = iRect.xy - grow + corner * (iRect.zw + 2.0 * grow);
    vHalf = iRect.zw * 0.5;
    vLocal = pos - (iRect.xy + vHalf);
    vUv = mix(iUv.xy, iUv.zw, corner);
    vColor = iColor;
    vParams = iParams;
    gl_Position = vec4(pos / pc.viewport * 2.0 - 1.0, 0.0, 1.0);
}
