#version 450
#extension GL_GOOGLE_include_directive : require
// A layer composited through the overscroll stretch: its own program, as HWUI draws
// StretchEffect with a dedicated runtime shader.
#define KILN_STRETCH
#include "quad_body.glsl"
