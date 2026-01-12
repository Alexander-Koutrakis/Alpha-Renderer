#version 450
//=============================================================================
// COMPOSITION PASS - FINAL TONEMAPPING AND BLENDING
//=============================================================================
//
// Purpose: Combine all render passes into final displayable image.
// Handles order-independent transparency (OIT).
//
// Input Textures:
//   - opaqueTexture: Direct lighting result (PBR + shadows)
//   - accumulationTexture: Weighted sum of transparent surfaces
//   - revealageTexture: Transparency revealage factor
//   - giTexture: Indirect diffuse GI from Radiance Cascades
//
// OIT Compositing:
//   Follows McGuire & Bavoil's Weighted Blended OIT paper.
//   For each pixel:
//     - If reveal = 1.0: Only opaque geometry, use opaqueTexture + GI
//     - If reveal < 1.0: Blend transparent over opaque using:
//       final = transparent * (1 - reveal) + (opaque + GI) * reveal
//
//=============================================================================


layout(binding = 0) uniform sampler2D opaqueTexture; // The result from light pass
layout(binding = 1) uniform sampler2D accumulationTexture; // Weighted sum for transparent objects
layout(binding = 2) uniform sampler2D revealageTexture; // Revealage factor for transparent objects
layout(binding = 3) uniform sampler2D giTexture; // Indirect Diffuse GI from Radiance Cascades

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;



void main() {

    vec4 opaqueColor = texture(opaqueTexture, inUV);
    float reveal = texture(revealageTexture, inUV).r;
    vec3 giColor = texture(giTexture, inUV).rgb;
    
    vec3 combinedOpaque = opaqueColor.rgb + giColor;
    vec3 hdrColor = combinedOpaque;

    // If reveal is 1.0, there are no transparent fragments at this pixel
    if (reveal < 0.9999) {
        vec4 accum = texture(accumulationTexture, inUV);
        float weightSum = max(1e-4, min(5e4, accum.a));
        vec3 transparentColor = accum.rgb / weightSum;
        
        hdrColor = transparentColor * (1.0 - reveal) + combinedOpaque * reveal;
    }

    outColor = vec4(hdrColor, 1.0);
} 