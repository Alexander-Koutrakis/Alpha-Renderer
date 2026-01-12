#version 450

layout(binding = 0) uniform sampler2D uColor;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outEdges;

/**
 * SMAA 1x – Luma Edge Detection (Pass 1)
 * Ported from the official SMAA reference (`SMAA.hlsl`) for Vulkan GLSL.
 *
 * Notes:
 * - `uColor` is expected to be **non-sRGB** (you said RGBA16F, good).
 * - We use `texelFetch` to force **point sampling** regardless of sampler state.
 * - Output channels match SMAA convention: R = left edge, G = top edge.
 * - If you use an `R8G8_UNORM` edge attachment, only `.rg` will be stored.
 */

const vec3  SMAA_LUMA_WEIGHTS = vec3(0.2126, 0.7152, 0.0722);
const float SMAA_THRESHOLD = 0.02; // "Ultra" preset equivalent
const float SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR = 2.0;

float lumaFromRGB(vec3 rgb) {
    return dot(rgb, SMAA_LUMA_WEIGHTS);
}

void main() {
    ivec2 size = textureSize(uColor, 0);
    ivec2 pix  = ivec2(gl_FragCoord.xy);

    // Clamp neighbors to avoid undefined reads on borders.
    // (Reference relies on clamped sampling; `texelFetch` doesn't clamp.)
    ivec2 left      = clamp(pix + ivec2(-1,  0), ivec2(0), size - 1);
    ivec2 top       = clamp(pix + ivec2( 0, -1), ivec2(0), size - 1);
    ivec2 right     = clamp(pix + ivec2( 1,  0), ivec2(0), size - 1);
    ivec2 bottom    = clamp(pix + ivec2( 0,  1), ivec2(0), size - 1);
    ivec2 leftLeft  = clamp(pix + ivec2(-2,  0), ivec2(0), size - 1);
    ivec2 topTop    = clamp(pix + ivec2( 0, -2), ivec2(0), size - 1);

    float L         = lumaFromRGB(texelFetch(uColor, pix,      0).rgb);
    float Lleft     = lumaFromRGB(texelFetch(uColor, left,     0).rgb);
    float Ltop      = lumaFromRGB(texelFetch(uColor, top,      0).rgb);

    // Initial left/top thresholding:
    vec2 deltaLT = abs(L - vec2(Lleft, Ltop));
    vec2 edges = step(vec2(SMAA_THRESHOLD), deltaLT);

    // Early out if no edge:
    if (dot(edges, vec2(1.0)) == 0.0) {
        outEdges = vec4(0.0);
        return;
    }

    // Calculate right/bottom deltas:
    float Lright    = lumaFromRGB(texelFetch(uColor, right,    0).rgb);
    float Lbottom   = lumaFromRGB(texelFetch(uColor, bottom,   0).rgb);
    vec2 deltaRB = abs(L - vec2(Lright, Lbottom));

    // Max delta in direct neighborhood:
    vec2 maxDelta = max(deltaLT, deltaRB);

    // Calculate left-left/top-top deltas:
    float Lleftleft = lumaFromRGB(texelFetch(uColor, leftLeft, 0).rgb);
    float Ltoptop   = lumaFromRGB(texelFetch(uColor, topTop,   0).rgb);
    vec2 deltaLLTT  = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));

    // Final maximum delta:
    maxDelta = max(maxDelta, deltaLLTT);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation:
    edges *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * deltaLT);

    // Output:
    // R = left edge (vertical line), G = top edge (horizontal line)
    outEdges = vec4(edges, 0.0, 0.0);
}
