#version 450

layout(binding = 0) uniform sampler2D uEdges;
layout(binding = 1) uniform sampler2D uAreaTex;
layout(binding = 2) uniform sampler2D uSearchTex;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outWeights;

/**
 * SMAA 1x – Blending Weight Calculation (Pass 2)
 * Ported (and simplified to non-diagonal) from the original SMAA reference:
 *   https://www.iryoku.com/smaa/
 *
 * Inputs:
 * - uEdges:    VK_FORMAT_R8G8_UNORM, edge texture from pass 1
 * - uAreaTex:  160x560 VK_FORMAT_R8G8_UNORM LUT
 * - uSearchTex:64x16   VK_FORMAT_R8_UNORM LUT
 *
 * Vulkan UVs in this project:
 * - `fullscreen.vert` maps `inUV=(0,0)` to the top-left.
 *
 * IMPORTANT (matches SMAA reference requirements):
 * - uEdges/uAreaTex/uSearchTex should use LINEAR filtering + CLAMP.
 *
 * Output channel packing (matches the SMAA reference neighborhood blending):
 * - R: bottom weight
 * - G: top weight
 * - B: left weight
 * - A: right weight
 */

// Quality/perf knobs (SMAA preset-like values):
const int   SMAA_MAX_SEARCH_STEPS = 32;

// LUT constants (fixed by the shipped textures):
const float SMAA_AREATEX_MAX_DISTANCE = 16.0;
const vec2  SMAA_AREATEX_SIZE         = vec2(160.0, 560.0);
const vec2  SMAA_AREATEX_PIXEL_SIZE   = 1.0 / SMAA_AREATEX_SIZE;
const float SMAA_AREATEX_SUBTEX_SIZE  = 1.0 / 7.0;

// Search LUT constants (from SMAA reference):
const vec2 SMAA_SEARCHTEX_SIZE        = vec2(66.0, 33.0);
const vec2 SMAA_SEARCHTEX_PACKED_SIZE = vec2(64.0, 16.0);

// Corner rounding (reference default is 25):
const float SMAA_CORNER_ROUNDING      = 25.0;
const float SMAA_CORNER_ROUNDING_NORM = SMAA_CORNER_ROUNDING / 100.0;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec2  saturate(vec2  x) { return clamp(x, vec2(0.0), vec2(1.0)); }

vec4 sampleLod0(sampler2D tex, vec2 uv) { return textureLod(tex, uv, 0.0); }
vec4 sampleLod0Offset(sampler2D tex, vec2 uv, ivec2 pixelOffset, vec2 invRes) {
    return textureLod(tex, uv + vec2(pixelOffset) * invRes, 0.0);
}

float SMAA_SearchTexSelect(vec4 s) { return s.r; }
vec2  SMAA_AreaTexSelect(vec4 s)   { return s.rg; }

float SMAASearchLength(vec2 e, float offset) {
    // Note: flipped vertically, with left/right cases taking half horizontally.
    vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);
    vec2 bias  = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);

    // Scale and bias to access texel centers:
    scale += vec2(-1.0,  1.0);
    bias  += vec2( 0.5, -0.5);

    // Convert from pixel coordinates to UVs (packed/cropped texture):
    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias  *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

    return SMAA_SearchTexSelect(sampleLod0(uSearchTex, scale * e + bias));
}

float SMAASearchXLeft(vec2 texcoord, float end, vec2 invRes) {
    vec2 e = vec2(0.0, 1.0);
    while (texcoord.x > end && e.g > 0.8281 && e.r == 0.0) {
        e = sampleLod0(uEdges, texcoord).rg;
        texcoord += (-vec2(2.0, 0.0)) * invRes;
    }
    float offset = (-(255.0 / 127.0)) * SMAASearchLength(e, 0.0) + 3.25;
    return invRes.x * offset + texcoord.x;
}

float SMAASearchXRight(vec2 texcoord, float end, vec2 invRes) {
    vec2 e = vec2(0.0, 1.0);
    while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0) {
        e = sampleLod0(uEdges, texcoord).rg;
        texcoord += (vec2(2.0, 0.0)) * invRes;
    }
    float offset = (-(255.0 / 127.0)) * SMAASearchLength(e, 0.5) + 3.25;
    return (-invRes.x) * offset + texcoord.x;
}

float SMAASearchYUp(vec2 texcoord, float end, vec2 invRes) {
    vec2 e = vec2(1.0, 0.0);
    while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0) {
        e = sampleLod0(uEdges, texcoord).rg;
        texcoord += (-vec2(0.0, 2.0)) * invRes;
    }
    float offset = (-(255.0 / 127.0)) * SMAASearchLength(e.gr, 0.0) + 3.25;
    return invRes.y * offset + texcoord.y;
}

float SMAASearchYDown(vec2 texcoord, float end, vec2 invRes) {
    vec2 e = vec2(1.0, 0.0);
    while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0) {
        e = sampleLod0(uEdges, texcoord).rg;
        texcoord += (vec2(0.0, 2.0)) * invRes;
    }
    float offset = (-(255.0 / 127.0)) * SMAASearchLength(e.gr, 0.5) + 3.25;
    return (-invRes.y) * offset + texcoord.y;
}

vec2 SMAAArea(vec2 dist, float e1, float e2, float subtexOffset) {
    // Rounding prevents precision errors of bilinear filtering:
    vec2 texcoord = vec2(SMAA_AREATEX_MAX_DISTANCE) * round(4.0 * vec2(e1, e2)) + dist;

    // Scale and bias to texel space:
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;

    // Select proper subtexture row (temporal modes). For SMAA 1x: 0.
    texcoord.y = SMAA_AREATEX_SUBTEX_SIZE * subtexOffset + texcoord.y;

    return SMAA_AreaTexSelect(sampleLod0(uAreaTex, texcoord));
}

void SMAADetectHorizontalCornerPattern(inout vec2 weights, vec4 texcoord, vec2 d, vec2 invRes) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;
    rounding /= (leftRight.x + leftRight.y); // reduce blending for center pixels

    vec2 factor = vec2(1.0);
    factor.x -= rounding.x * sampleLod0Offset(uEdges, texcoord.xy, ivec2(0,  1), invRes).r;
    factor.x -= rounding.y * sampleLod0Offset(uEdges, texcoord.zw, ivec2(1,  1), invRes).r;
    factor.y -= rounding.x * sampleLod0Offset(uEdges, texcoord.xy, ivec2(0, -2), invRes).r;
    factor.y -= rounding.y * sampleLod0Offset(uEdges, texcoord.zw, ivec2(1, -2), invRes).r;

    weights *= saturate(factor);
}

void SMAADetectVerticalCornerPattern(inout vec2 weights, vec4 texcoord, vec2 d, vec2 invRes) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;
    rounding /= (leftRight.x + leftRight.y);

    vec2 factor = vec2(1.0);
    factor.x -= rounding.x * sampleLod0Offset(uEdges, texcoord.xy, ivec2( 1, 0), invRes).g;
    factor.x -= rounding.y * sampleLod0Offset(uEdges, texcoord.zw, ivec2( 1, 1), invRes).g;
    factor.y -= rounding.x * sampleLod0Offset(uEdges, texcoord.xy, ivec2(-2, 0), invRes).g;
    factor.y -= rounding.y * sampleLod0Offset(uEdges, texcoord.zw, ivec2(-2, 1), invRes).g;

    weights *= saturate(factor);
}

void main() {
    ivec2 size = textureSize(uEdges, 0);
    vec2 invRes = 1.0 / vec2(size);
    vec4 rtMetrics = vec4(invRes, vec2(size));

    // Recreate SMAABlendingWeightCalculationVS outputs:
    vec2 pixcoord = inUV * rtMetrics.zw;
    vec4 offset0  = inUV.xyxy + rtMetrics.xyxy * vec4(-0.25, -0.125,  1.25, -0.125);
    vec4 offset1  = inUV.xyxy + rtMetrics.xyxy * vec4(-0.125, -0.25,  -0.125,  1.25);
    vec4 offset2  = vec4(offset0.x, offset0.z, offset1.y, offset1.w) +
                    rtMetrics.xxyy * (vec4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS));

    vec4 weights = vec4(0.0);
    vec2 e = sampleLod0(uEdges, inUV).rg;

    // Edge at north (top): compute vertical (bottom/top) weights into R/G.
    if (e.g > 0.0) {
        vec2 d;
        vec3 coords;

        // Distance to the left:
        coords.x = SMAASearchXLeft(offset0.xy, offset2.x, invRes);
        coords.y = offset1.y; // texcoord.y - 0.25 * invRes.y (@CROSSING_OFFSET)
        d.x = coords.x;

        // Left crossing edge:
        float e1 = sampleLod0(uEdges, coords.xy).r;

        // Distance to the right:
        coords.z = SMAASearchXRight(offset0.zw, offset2.y, invRes);
        d.y = coords.z;

        // Distances in pixels:
        d = abs(round(rtMetrics.zz * d - pixcoord.xx));
        vec2 sqrt_d = sqrt(d);

        // Right crossing edge:
        float e2 = sampleLod0Offset(uEdges, coords.zy, ivec2(1, 0), invRes).r;

        weights.rg = SMAAArea(sqrt_d, e1, e2, 0.0);

        // Corner fix:
        coords.y = inUV.y;
        SMAADetectHorizontalCornerPattern(weights.rg, coords.xyzy, d, invRes);
    }

    // Edge at west (left): compute horizontal (left/right) weights into B/A.
    if (e.r > 0.0) {
        vec2 d;
        vec3 coords;

        // Distance to the top:
        coords.y = SMAASearchYUp(offset1.xy, offset2.z, invRes);
        coords.x = offset0.x; // texcoord.x - 0.25 * invRes.x
        d.x = coords.y;

        // Top crossing edge:
        float e1 = sampleLod0(uEdges, coords.xy).g;

        // Distance to the bottom:
        coords.z = SMAASearchYDown(offset1.zw, offset2.w, invRes);
        d.y = coords.z;

        // Distances in pixels:
        d = abs(round(rtMetrics.ww * d - pixcoord.yy));
        vec2 sqrt_d = sqrt(d);

        // Bottom crossing edge:
        float e2 = sampleLod0Offset(uEdges, coords.xz, ivec2(0, 1), invRes).g;

        weights.ba = SMAAArea(sqrt_d, e1, e2, 0.0);

        // Corner fix:
        coords.x = inUV.x;
        SMAADetectVerticalCornerPattern(weights.ba, coords.xyxz, d, invRes);
    }

    outWeights = weights;
}
