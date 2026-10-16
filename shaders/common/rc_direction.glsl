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
        vec2 folded = (1.0 - abs(vec2(v.y, v.x))) * sign(vec2(v.x, v.y));
        v = vec3(folded.x, folded.y, 0.0);
    }
    v = normalize(v);
    return v;
}

// Inverse of hemisphereDirFromUV for directions with z > 0; (-1, -1) when the direction has no texel.
vec2 hemisphereUVFromDir(vec3 dir, float hemiShrink) {
    vec3 d = normalize(dir);
    if (d.z <= 0.0) return vec2(-1.0);
    float denom = abs(d.x) + abs(d.y) + d.z;
    if (denom < 0.0001) return vec2(-1.0);
    vec2 p = vec2(d.x, d.y) / denom;
    vec2 uv = 0.5 * (p + 1.0);
    uv = 0.5 + (uv - 0.5) / hemiShrink;
    return clamp(uv, vec2(0.001), vec2(0.999));
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
