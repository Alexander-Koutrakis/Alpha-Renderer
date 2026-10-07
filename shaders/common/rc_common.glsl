// Radiance Cascades helpers shared by rc_build_cascade, rc_merge and rc_resolve_indirect.
// Include with GL_GOOGLE_include_directive.
#ifndef ALPHA_COMMON_RC_COMMON_GLSL
#define ALPHA_COMMON_RC_COMMON_GLSL

vec3 decodeNormal(vec3 encoded) {
    return normalize(encoded * 2.0 - 1.0);
}

mat3 buildProbeBasis(vec3 n) {
    vec3 normal = normalize(n);
    vec3 up = (abs(normal.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return mat3(tangent, bitangent, normal);
}

// Hemisphere octahedral tile shrink: 2 -> 0.90, 8+ -> 0.98, smooth in between.
float computeHemisphereShrink(int tileSize) {
    float t = clamp((float(tileSize) - 2.0) / 6.0, 0.0, 1.0);
    return mix(0.90, 0.98, t);
}

// Upper-hemisphere octahedral mapping with shrink toward center.
// UV in [0,1]^2 -> local direction with z >= 0.
vec3 hemisphereDirFromUV(vec2 uv, float hemiShrink) {
    uv = mix(vec2(0.5), uv, hemiShrink);
    vec2 p = uv * 2.0 - 1.0;               // [-1,1]
    vec3 v = vec3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    if (v.z < 0.0) {                       // fold back into upper hemisphere
        v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
        v.z = 0.0;
    }
    v = normalize(v);
    return v;
}

ivec2 getProbePixel(ivec2 probeIndex, int probeStridePx) {
    return probeIndex * probeStridePx + probeStridePx / 2;
}

#endif
