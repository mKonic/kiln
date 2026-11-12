#version 450
#extension GL_GOOGLE_include_directive : require
// Solid (rounded) rects with a flat colour, no colour filter or blend mode: most of a frame's
// quads. A small shader for them, as Impeller's per-content pipelines and HWUI's per-draw
// programs do; a software rasteriser runs the whole uber-shader's code per pixel otherwise.
#define KILN_SOLID
#include "quad_body.glsl"
