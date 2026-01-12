#version 450

layout(binding = 0) uniform sampler2D uColor;
layout(binding = 1) uniform sampler2D uBlendWeights;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    // Reference SMAA neighborhood blending.
    // Expects weights packing from `smaa_weight.frag`:
    //   R: bottom, G: top, B: left, A: right
    vec2 texel = 1.0 / vec2(textureSize(uColor, 0));

    // Equivalent to SMAANeighborhoodBlendingVS offset:
    vec4 offset = inUV.xyxy + vec4(texel.x, 0.0, 0.0, texel.y);

    // Fetch the blending weights for current pixel:
    vec4 a;
    a.x  = texture(uBlendWeights, offset.xy).a; // Right
    a.y  = texture(uBlendWeights, offset.zw).g; // Top
    a.wz = texture(uBlendWeights, inUV).xz;     // Bottom / Left

    // Is there any blending weight with a value greater than 0?
    if (dot(a, vec4(1.0)) < 1e-5) {
        outColor = texture(uColor, inUV);
        return;
    }

    bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)

    // Calculate the blending offsets:
    vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);
    vec2 blendingWeight = vec2(a.y, a.w);
    if (h) {
        blendingOffset = vec4(a.x, 0.0, a.z, 0.0);
        blendingWeight = vec2(a.x, a.z);
    }
    blendingWeight /= max(dot(blendingWeight, vec2(1.0)), 1e-8);

    // Calculate the texture coordinates:
    vec4 blendingCoord = blendingOffset * vec4(texel, -texel) + inUV.xyxy;

    // Exploit bilinear filtering to mix current pixel with the chosen neighbor:
    outColor = blendingWeight.x * texture(uColor, blendingCoord.xy) +
               blendingWeight.y * texture(uColor, blendingCoord.zw);
}
