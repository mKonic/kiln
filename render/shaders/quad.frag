#version 450
layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vColor;
layout(location = 3) flat in vec4 vParams;
layout(location = 4) flat in vec2 vHalf;

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) out vec4 outColor;

float roundedRectSdf(vec2 p, vec2 half_, float r) {
    r = min(r, min(half_.x, half_.y));
    vec2 q = abs(p) - half_ + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    int mode = int(vParams.y + 0.5);
    if (mode == 0) {            // solid rounded rect, analytic coverage
        float d = roundedRectSdf(vLocal, vHalf, vParams.x);
        float coverage = clamp(0.5 - d, 0.0, 1.0);
        outColor = vColor * coverage;
    } else if (mode == 1) {     // atlas coverage tinted by colour (text)
        outColor = vColor * texture(uAtlas, vUv).a;
    } else {                    // atlas colour (emoji, images), premultiplied, alpha-modulated
        outColor = texture(uAtlas, vUv) * vColor.a;
    }
}
