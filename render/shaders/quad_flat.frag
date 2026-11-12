#version 450
#extension GL_GOOGLE_include_directive : require
// Unclipped rects whose edges lie on whole device pixels, in a flat colour: every covered pixel is
// fully inside, so no coverage is computed (Skia's non-anti-aliased rect fill).
#define KILN_FLAT
#include "quad_body.glsl"
