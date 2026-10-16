// Radiance Cascades direction mapping: probe frame, tile texel <-> hemisphere direction, merge texel mapping.
// Shared by rc_build_cascade, rc_merge and rc_resolve_indirect (through rc_common.glsl) and compiled as C++ by
// tests/rc_direction_test.cpp, so the tests run the real shader code. Keep it to the GLSL/glm common subset: no swizzle
// assignment, no out parameters, no #include.
#ifndef ALPHA_COMMON_RC_DIRECTION_GLSL
#define ALPHA_COMMON_RC_DIRECTION_GLSL

mat3 buildProbeBasis(vec3 n) {
    vec3 normal = normalize(n);
    vec3 up = (abs(normal.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return mat3(tangent, bitangent, normal);
}

// Hemi-octahedral mapping (Cigolle et al.): the whole unit square of a tile is the upper hemisphere, no texel is wasted
// and none lies exactly on the horizon. The square's centre is the normal, its edges are the horizon. The mapping is the
// same for every tile size, so a near texel and the far texels covering it point the same way.
// UV in [0,1]^2 -> unit direction with z > 0 (z = 0 only on the square's boundary).
vec3 hemisphereDirFromUV(vec2 uv) {
    vec2 e = uv * 2.0 - 1.0;                              // [-1,1]
    vec2 p = vec2(e.x + e.y, e.x - e.y) * 0.5;            // |p.x| + |p.y| = max(|e.x|, |e.y|) <= 1
    return normalize(vec3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y)));
}

// Inverse of hemisphereDirFromUV for directions with z > 0; (-1, -1) when the direction has no texel.
vec2 hemisphereUVFromDir(vec3 dir) {
    vec3 d = normalize(dir);
    if (d.z <= 0.0) return vec2(-1.0);
    vec2 p = vec2(d.x, d.y) / (abs(d.x) + abs(d.y) + d.z);
    vec2 e = vec2(p.x + p.y, p.x - p.y);
    return clamp(e * 0.5 + 0.5, vec2(0.001), vec2(0.999));
}

// Merge: where a far probe holds the direction a near texel represents. Every probe has its own tangent frame, so the
// direction is converted through world space: the far texel is found from the world direction, never by tile coordinate.
struct FarTileLookup {
    ivec2 base;    // first of the 2x2 far texels to interpolate
    vec2 frac;     // bilinear weight inside that block
    float localZ;  // cosine of the angle between the direction and the far probe's normal; <= 0: below its horizon
};

FarTileLookup lookupFarTile(vec3 worldDir, mat3 farBasis, int farTileSize) {
    vec3 local = transpose(farBasis) * worldDir;
    FarTileLookup result;
    result.localZ = local.z;
    vec2 uv = hemisphereUVFromDir(normalize(vec3(local.x, local.y, max(local.z, 0.01))));
    vec2 f = uv * float(farTileSize) - 0.5;                // texel centres at integers
    result.base = clamp(ivec2(floor(f)), ivec2(0), ivec2(farTileSize - 2));
    result.frac = clamp(f - vec2(result.base), vec2(0.0), vec2(1.0));
    return result;
}

// How much a probe can say about a direction, from the direction's cosine to the probe's normal.
float probeHorizonWeight(float localZ) {
    return smoothstep(-0.1, 0.2, localZ);
}

// Resolve: the directions one pixel integrates over are the cells of a cellCounts grid on its own hemisphere map, plus a
// small per-frame jitter in cell units.
vec3 resolveSampleDirection(ivec2 cell, ivec2 cellCounts, vec2 jitter) {
    vec2 uv = (vec2(cell) + 0.5 + jitter) / vec2(cellCounts);
    return hemisphereDirFromUV(clamp(uv, vec2(0.02), vec2(0.98)));
}

// Irradiance E = integral of L cos(theta) dOmega from samples, sumLCos = sum of L_i * cos_i and sumCos = sum of cos_i.
// The map's texels do not subtend equal solid angles, so a fixed 2*PI/N weight is biased (+41% for a 2x2 grid). Dividing by
// the cosine sum is the cosine-weighted mean of L times PI, which is exact for a uniform field whatever the sample layout.
const float RC_PI = 3.14159265359;
vec3 irradianceFromSamples(vec3 sumLCos, float sumCos) {
    return RC_PI * sumLCos / sumCos;
}

#endif
