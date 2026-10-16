#version 450
#extension GL_GOOGLE_include_directive : require
//=============================================================================
// DEFERRED DIRECT LIGHTING PASS
//=============================================================================
//
// Overview:
//   Full-screen lighting pass operating on G-Buffer data. Evaluates direct
//   lighting from all scene lights using physically-based shading.
//
// BRDF Model: Cook-Torrance Microfacet Specular + Lambertian Diffuse
//   The Cook-Torrance BRDF models surfaces as collections of tiny mirrors
//   (microfacets). Three terms control the specular response:
//
//   D (Normal Distribution Function) - GGX/Trowbridge-Reitz:
//     Models how microfacet normals are distributed around the surface normal.
//     Rougher surfaces → wider distribution → blurrier highlights.
//     GGX has a "long tail" that creates realistic highlight falloff.
//
//   F (Fresnel) - Schlick Approximation:
//     Models how reflectivity increases at grazing angles. All materials
//     become ~100% reflective at 90° (edge-on). F0 = reflectance at normal
//     incidence (0.04 for dielectrics, albedo color for metals).
//
//   G (Geometry/Masking-Shadowing) - Smith with Schlick-GGX:
//     Models self-shadowing of microfacets. At grazing angles, some facets
//     are occluded by others, reducing specular response.
//
//   Final BRDF:
//     f_specular = (D × F × G) / (4 × NdotL × NdotV)
//     f_diffuse  = albedo / π  (Lambertian)
//     f_total    = kD × f_diffuse + kS × f_specular
//
//   Energy Conservation:
//     kS = F (fresnel determines specular contribution)
//     kD = (1 - kS) × (1 - metallic) (only non-metals have diffuse)
//
// Shadow Mapping:
//   - Directional: Cascaded Shadow Maps (4 cascades) with smooth blending
//   - Spot:        Standard shadow map with perspective projection
//   - Point:       Cubemap shadow (omnidirectional)
//   - PCF:         16-sample rotated Poisson disk for soft shadows
//
// Image-Based Lighting:
//   - Diffuse:  Sample lowest mip of environment map (approximates irradiance)
//   - Specular: Sample mip level based on roughness (prefiltered environment)
//
// References:
//   - Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013
//   - Walter et al., "Microfacet Models for Refraction", EGSR 2007 (GGX)
//   - Schlick, "An Inexpensive BRDF Model for Physically-based Rendering", 1994
//
//=============================================================================

//=============================================================================
// CONSTANTS
//=============================================================================
#include "light.glsl"
#include "brdf.glsl"

const vec3 ambientColor = vec3(0.02, 0.02, 0.02);

//=============================================================================
// I/O
//=============================================================================
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outIncident;

//=============================================================================
// STRUCTURES
//=============================================================================
//=============================================================================
// UNIFORMS
//=============================================================================
layout(set = 0, binding = 0) uniform EnvironmentLightingUbo {
    mat4 viewMatrix;
    mat4 projectionMatrix;
    vec4 cameraPosition;
    float ambientIntensity;
    float reflectionIntensity;
} environmentLighting;

layout(set = 1, binding = 0) uniform LightUbo {
    Light lights[MAX_LIGHTS];
    int lightCount;
} unifiedLights;

layout(set = 2, binding = 0) uniform sampler2D positionTexture;
layout(set = 2, binding = 1) uniform sampler2D normalTexture;
layout(set = 2, binding = 2) uniform sampler2D albedoTexture;
layout(set = 2, binding = 3) uniform sampler2D materialTexture;

layout(set = 3, binding = 0) uniform sampler2DArray directionalShadowMaps[MAX_SHADOWCASTING_DIRECTIONAL];
layout(set = 3, binding = 1) uniform sampler2D spotShadowMaps[MAX_SHADOWCASTING_SPOT];
layout(set = 3, binding = 2) uniform samplerCube pointShadowMaps[MAX_SHADOWCASTING_POINT];

layout(set = 4, binding = 0) uniform ShadowcastingLightMatrices {
    mat4 shadowcastingLightMatrices[MAX_SHADOWCASTING_LIGHT_MATRICES];
} lightMatrices;

layout(set = 5, binding = 0) uniform samplerCube environmentMap;

layout(set = 6, binding = 0) uniform DirectionalLightCascadeSplits {
    vec4 cascadeSplits[MAX_SHADOWCASTING_DIRECTIONAL];
} directionalCascadeSplits;

//=============================================================================
// SHADOWS
//=============================================================================
float shadowViewDepth(vec3 worldPos) {
    return abs((environmentLighting.viewMatrix * vec4(worldPos, 1.0)).z);
}

#include "shadow.glsl"

//=============================================================================
// COOK-TORRANCE BRDF
// Physically-based specular BRDF using GGX/Trowbridge-Reitz distribution
//=============================================================================

/// Simple energy-compensation term so rough specular lobes don't lose too much energy
float SpecularEnergyCompensation(float roughness) {
    float r2 = roughness * roughness;
    return 1.0 + r2 * (1.0 - 0.5 * roughness);
}

//=============================================================================
// IMAGE-BASED LIGHTING (non-physical, unitless)
//=============================================================================

vec3 sampleDiffuseIBL(vec3 normal) {
    // Sample the lowest mip to approximate an irradiance-like blur
    float mipCount = float(textureQueryLevels(environmentMap));
    float diffuseMip = max(mipCount - 1.0, 0.0);
    return textureLod(environmentMap, normal, diffuseMip).rgb;
}

vec3 calculateIBLSpecular(vec3 normal, vec3 viewDir, vec3 albedo, float roughness, float metallic) {
    vec3 R = normalize(reflect(-viewDir, normal));
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float mipCount = float(textureQueryLevels(environmentMap));
    float mipLevel = roughness * max(mipCount - 1.0, 0.0);
    vec3 prefilteredColor = textureLod(environmentMap, R, mipLevel).rgb;

    float NdotV = max(dot(normal, viewDir), 0.0);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);

    // Simple energy compensation so rough reflections don't vanish entirely
    float energyComp = 1.0 - roughness * 0.5;
    return prefilteredColor * F * energyComp;
}

//=============================================================================
// LIGHT ATTENUATION
// Controls how light intensity falls off with distance
//=============================================================================

/// Distance attenuation using quartic falloff curve
/// Formula: ((1 - d/r)^4) * smoothWindowFactor
/// Provides steep falloff that reaches zero at the light's range
/// Unity URP-style distance attenuation
/// Uses inverse-square falloff (1/d²) with smooth window at range boundary
float DistanceAttenuation(float distanceSqr, vec2 distanceAndSpotAttenuation) {
    float invRangeSqr = distanceAndSpotAttenuation.x;
    
    // Unity URP uses inverse-square falloff (physically accurate)
    // This allows light to get brighter than 1.0 close to source
    float lightAtten = 1.0 / max(distanceSqr, 0.0001);
    
    // Smooth window to reach zero at range boundary
    // factor = (d/range)²
    float factor = distanceSqr * invRangeSqr;
    float smoothFactor = saturate(1.0 - factor * factor);
    smoothFactor = smoothFactor * smoothFactor;
    
    return lightAtten * smoothFactor;
}

/// Unified attenuation for all light types
float UnifiedAttenuation(int lightType, float distanceSqr, vec3 spotDirection,
                         vec3 lightDirection, vec4 attenParams) {
    float invRangeSqr = attenParams.x;
    vec2 distanceParams = vec2(invRangeSqr, 1.0);
    vec2 spotParams = attenParams.zw;

    if (lightType == 0) return 1.0;  // Directional
    if (lightType == 2) return DistanceAttenuation(distanceSqr, distanceParams);  // Point
    
    // Spot: distance * angle
    return DistanceAttenuation(distanceSqr, distanceParams) * 
           AngleAttenuation(spotDirection, lightDirection, spotParams);
}

//=============================================================================
// MAIN LIGHTING CALCULATION
//=============================================================================

/// Calculates the lighting contribution from a single light source
/// Combines diffuse (Lambert) and specular (Cook-Torrance) BRDF with shadows
vec3 calculateUnifiedLight(Light light, vec3 worldPos, vec3 normal, vec3 viewDir,
                           vec3 albedo, float roughness, float metallic,
                           vec3 F0, vec3 kS, vec3 kD,
                           out vec3 incidentDiffuse) {
    // Light vector: for directional w=0, for punctual w=1
    vec3 lightVector = light.positionAndData.xyz - worldPos * light.positionAndData.w;
    float distanceSqr = max(dot(lightVector, lightVector), EPSILON);
    vec3 lightDirection = lightVector * inversesqrt(distanceSqr);
    
    float attenuation = UnifiedAttenuation(light.lightType, distanceSqr,
                                           -light.directionAndRange.xyz,
                                           lightDirection, light.attenuationParams);
    
    float NdotL = max(dot(normal, lightDirection), 0.0);
    incidentDiffuse = vec3(0.0);
    if (NdotL <= 0.0 || attenuation <= 0.0) return vec3(0.0);
    
    float shadow = calculateShadow(light, worldPos, normal);
    float NdotV = max(dot(normal, viewDir), 0.0);
    vec3 halfVector = normalize(lightDirection + viewDir);
    
    vec3 diffuse = kD * albedo / PI;
    vec3 specularBRDF = cookTorranceBRDF(normal, viewDir, lightDirection, halfVector,
                                          F0, roughness, NdotL, NdotV);
    specularBRDF *= SpecularEnergyCompensation(roughness);
    
    // Unity URP uses intensity directly without physical normalization
    float intensity = light.colorAndIntensity.a;
    
    vec3 lightColor = light.colorAndIntensity.rgb * intensity;
    vec3 lightFactor = lightColor * NdotL * attenuation * shadow;
    incidentDiffuse = lightFactor; // irradiance-like, pre-albedo, pre-kD
    return (diffuse + specularBRDF) * lightFactor;
}

//=============================================================================
// MAIN
//=============================================================================
void main() {
    vec3 albedo = texture(albedoTexture, inUV).rgb;
    vec4 positionSample = texture(positionTexture, inUV);
    vec3 worldPos = positionSample.xyz;
    vec3 normal = normalize(texture(normalTexture, inUV).rgb * 2.0 - 1.0);
    vec4 material = texture(materialTexture, inUV);
    vec3 viewDir = normalize(environmentLighting.cameraPosition.xyz - worldPos);
    float metallic = material.r;
    float roughness = clamp(1.0-material.g, 0.045, 1.0);
    float ao = material.b;
 


    // Skip lighting for skybox pixels: the geometry pass clears position.w to 0 and writes 1 for surfaces.
    // (Testing length(worldPos) would also drop a surface sitting exactly at the world origin.)
    if (positionSample.w <= 0.0) {
        outColor = vec4(albedo, 1.0);
        outIncident = vec4(0.0);
        return;
    }
    
    // Energy conservation
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(normal, viewDir), 0.0);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    // Accumulate direct lighting
    vec3 directLighting = vec3(0.0);
    vec3 directIncident = vec3(0.0);
    for (int i = 0; i < unifiedLights.lightCount; ++i) {
        vec3 incident = vec3(0.0);
        directLighting += calculateUnifiedLight(unifiedLights.lights[i], worldPos, normal,
                                                viewDir, albedo, roughness, metallic, F0, kS, kD,
                                                incident);
        directIncident += incident;
    }

    // Add sky/ambient irradiance into the incident buffer so GI has energy on unlit walls.
    // This is pre-albedo, pre-kD by design (build shader applies albedo and kD).
    vec3 skyIrradiance = sampleDiffuseIBL(normal) * environmentLighting.ambientIntensity;
    directIncident += skyIrradiance;
    directIncident += ambientColor;
    vec3 iblDiffuse = skyIrradiance * albedo * kD * ao;
    // Specular IBL (reflection probe / skybox prefilter)
    vec3 iblSpecular = calculateIBLSpecular(normal, viewDir, albedo, roughness, metallic);
    iblSpecular *= environmentLighting.reflectionIntensity;

    vec3 indirect = iblDiffuse + iblSpecular;
    
    outColor = vec4(directLighting+indirect, 1.0);


    outIncident = vec4(directIncident, 1.0);
}
