// Light array limits, the Light struct (must match the C++ light UBO layout) and the
// light helpers both lighting shaders agree on. Include with GL_GOOGLE_include_directive.
#ifndef ALPHA_COMMON_LIGHT_GLSL
#define ALPHA_COMMON_LIGHT_GLSL

#include "math.glsl"

const int MAX_LIGHTS = 128;
const int MAX_CASCADE_COUNT = 4;
const int MAX_SHADOWCASTING_DIRECTIONAL = 4;
const int MAX_SHADOWCASTING_SPOT = 8;
const int MAX_SHADOWCASTING_POINT = 8;
const int MAX_SHADOWCASTING_LIGHT_MATRICES = 64;

struct Light {
    vec4 positionAndData;       // xyz=position, w=0 for directional, 1 for punctual
    vec4 colorAndIntensity;     // rgb=color, a=intensity
    vec4 directionAndRange;     // xyz=direction, w=range
    vec4 attenuationParams;     // x=invRangeSqr, y=unused, zw=spotAngleParams
    int lightType;              // 0=directional, 1=spot, 2=point
    int lightMatrixOffset;      // offset into the shadowcastingLightMatrices array
    int shadowmapIndex;         // index into the shadowmap array
    int isCastingShadow;        // 0=no, 1=yes
    float shadowStrength;
};

#define BEYOND_SHADOW_FAR(shadowCoord) (shadowCoord.z <= 0.0 || shadowCoord.z >= 1.0)

/// Spot light angular attenuation
/// Uses precomputed scale/offset for inner/outer cone falloff
float AngleAttenuation(vec3 spotDirection, vec3 lightDirection, vec2 spotAttenuation) {
    float SdotL = dot(spotDirection, lightDirection);
    float atten = saturate(SdotL * spotAttenuation.x + spotAttenuation.y);
    return atten * atten;
}

#endif
