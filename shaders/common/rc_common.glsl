// Radiance Cascades helpers shared by rc_build_cascade, rc_merge and rc_resolve_indirect.
// Include with GL_GOOGLE_include_directive.
#ifndef ALPHA_COMMON_RC_COMMON_GLSL
#define ALPHA_COMMON_RC_COMMON_GLSL

#include "rc_direction.glsl"

vec3 decodeNormal(vec3 encoded) {
    return normalize(encoded * 2.0 - 1.0);
}

ivec2 getProbePixel(ivec2 probeIndex, int probeStridePx) {
    return probeIndex * probeStridePx + probeStridePx / 2;
}

#endif
