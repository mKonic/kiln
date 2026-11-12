#version 450
#extension GL_GOOGLE_include_directive : require
// An embedded external surface's frames (AndroidEmbeddedExternalSurface): a hardware buffer,
// often sampled through a Y'CbCr conversion, which allows implicit-LOD sampling only.
#define KILN_EXTERNAL
#include "quad_body.glsl"
