#version 450
// One instanced unit quad per primitive. Rects are in the instance's local space; the affine
// transform maps them to framebuffer pixels (+y down).
layout(location = 0) in vec4 iRect;    // x, y, w, h (local)
layout(location = 1) in vec4 iUv;      // u0, v0, u1, v1 (atlas, normalised)
layout(location = 2) in vec4 iColor;   // premultiplied rgba
layout(location = 3) in vec4 iParams;  // cornerRadius (local), mode, filter row, unused
layout(location = 4) in vec4 iClip;    // clip rect x0, y0, x1, y1 (clip space)
layout(location = 5) in vec4 iClipParams; // clip corner radii: top-left, top-right, bottom-right, bottom-left
layout(location = 6) in vec4 iPaint;   // gradient geometry (framebuffer px)
layout(location = 7) in vec4 iPaint2;  // gradient kind, tile mode, ramp v
layout(location = 8) in vec4 iXform;   // a, b, c, d
layout(location = 9) in vec4 iXform2;  // e, f, then g, h: perspective (w = 1 + g x + h y; 0 = affine)
layout(location = 10) in vec4 iClipInv;  // framebuffer -> clip space: a, b, c, d
layout(location = 11) in vec4 iClipInv2; // e, f, clip enabled
layout(location = 12) in vec4 iClipOuter; // a plain clip rect too (framebuffer px)

layout(push_constant) uniform Push { vec2 viewport; vec2 origin; vec2 dstOrigin; } pc;   // target size, its device origin

layout(location = 0) out vec2 vLocal;      // local units from the rect centre
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vColor;
layout(location = 3) flat out vec4 vParams;
layout(location = 4) flat out vec2 vHalf;
layout(location = 5) flat out vec4 vClip;
layout(location = 6) flat out vec4 vClipParams;
layout(location = 7) flat out vec4 vPaint;
layout(location = 8) flat out vec4 vPaint2;
layout(location = 9) flat out vec4 vClipInv;
layout(location = 10) flat out vec4 vClipInv2;
layout(location = 11) flat out vec2 vUvMin;
layout(location = 12) flat out vec2 vUvMax;
layout(location = 13) flat out vec4 vClipOuter;

void main() {
    vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    mat2 m = mat2(iXform.x, iXform.y, iXform.z, iXform.w);
    // Grow solid shapes by one device pixel (in local units) so the anti-aliased edge has room.
    float scale = sqrt(max(abs(determinant(m)), 1e-8));
    float grow = iParams.y == 0.0 || iParams.y == 7.0 ? 1.0 / scale : 0.0;
    vec2 pos = iRect.xy - grow + corner * (iRect.zw + 2.0 * grow);
    vHalf = iRect.zw * 0.5;
    vLocal = pos - (iRect.xy + vHalf);
    vUv = mix(iUv.xy, iUv.zw, corner);
    vColor = iColor;
    vParams = iParams;
    vClip = iClip;
    vClipParams = iClipParams;
    vPaint = iPaint;
    vPaint2 = iPaint2;
    vClipInv = iClipInv;
    vClipInv2 = iClipInv2;
    vClipOuter = iClipOuter;
    vUvMin = iUv.xy;
    vUvMax = iUv.zw;
    // A projective transform (a 3D-rotated layer) divides by w; handing w to the rasteriser
    // keeps the interpolated local and texture coordinates perspective-correct.
    float w = 1.0 + dot(iXform2.zw, pos);
    vec2 device = (m * pos + iXform2.xy) / w;
    gl_Position = vec4(((device - pc.origin) / pc.viewport * 2.0 - 1.0) * w, 0.0, w);
}
