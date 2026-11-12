#version 450
#extension GL_GOOGLE_include_directive : require
// Glyphs, path masks and images (linear sampling) with a flat colour, no colour filter or blend
// mode: a small shader beside quad_solid.frag (see there).
#define KILN_COVERAGE
#include "quad_body.glsl"
