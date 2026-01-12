#version 450
//=============================================================================
// SHADOW MAP PASS
//=============================================================================
//
// Purpose: Render depth from the light's perspective to create shadow maps.
// Each light type requires different depth calculation and storage.
//
// Shadow Map Types:
//   - Directional: Standard depth buffer, uses hardware Z
//   - Spot:        Standard depth buffer, uses hardware Z
//   - Point:       Cubemap depth buffer, stores distance to light
//
// Depth Storage:
//   - Directional/Spot: gl_FragDepth = gl_FragCoord.z (NDC depth)
//   - Point: gl_FragDepth = distance(light, pixel) / lightRange (normalized distance)
//
// Alpha Masking:
//   - Transparent objects (foliage, chain-link) need alpha testing
//   - Objects with alpha < cutoff are discarded (create holes)
//   - Ensures shadows cast properly through semi-transparent surfaces
//
// Push Constants:
//   - lightPosRange: Light position (xyz) and range (w)
//   - matrixIndex: Which shadow matrix to use
//   - lightType: 0=directional, 1=spot, 2=point
//
// Performance Notes:
//   - Alpha testing happens early for early Z discard
//   - Point lights use expensive cubemap writes - minimize point light count
//
//=============================================================================

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inUV;

layout(push_constant) uniform PushConstants {
    vec4 lightPosRange;
    uint matrixIndex;
    uint instanceOffset;
    uint lightType; // 0=directional, 1=spot, 2=point
} push;

// Material set should remain set = 2 for shadowmap pass (matches pipeline layout)
layout(set = 2, binding = 0) uniform MaterialUbo {
    vec4 albedoColor;
    float metallic;
    float smoothness;
    float ao;
    float alphaCutoff;
    int isMasked;
    int isEmissive;
    int hasAlbedoMap;
    int hasMetallicSmoothnessMap;
    int hasNormalMap;
    int hasOcclusionMap;
    float normalStrength;
} material;

layout(set = 2, binding = 1) uniform sampler2D albedoMap;

void main() {
    // Alpha testing - moved to top for early exit
    if(material.isMasked == 1) {
        float alpha = texture(albedoMap, inUV).a;
        if(alpha < material.alphaCutoff) {
            discard;
        }
    }
    

    if(push.lightType == 0u) {
        // Directional - use hardware depth
        gl_FragDepth = gl_FragCoord.z;
    } else {
        // Point/Spot - calculate radial depth
        float lightDistance = length(inWorldPos - push.lightPosRange.xyz);
        gl_FragDepth = lightDistance / push.lightPosRange.w;
    }
}