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

// Merge: where a near-cascade tile texel reads the far cascade's tile (tile coordinates, no probe orientation applied).
struct FarTileMap {
    ivec2 base;   // first of the 2x2 far texels
    vec2 frac;    // bilinear weight inside that block
};

FarTileMap mapNearTexelToFarTile(ivec2 nearTile, int nearTileSize) {
    int farTileSize = nearTileSize * 2;
    vec2 nearTileUV = (vec2(nearTile) + 0.5) / float(nearTileSize);  // [0,1] within tile
    vec2 farTileFloat = nearTileUV * float(farTileSize) - 0.5;       // continuous far coord
    FarTileMap result;
    result.base = clamp(ivec2(floor(farTileFloat)), ivec2(0), ivec2(farTileSize - 2));
    result.frac = fract(farTileFloat);
    return result;
}

#endif
