// Cook-Torrance microfacet BRDF terms (GGX / Schlick-GGX / Smith / Schlick Fresnel).
// Include with GL_GOOGLE_include_directive.
#ifndef ALPHA_COMMON_BRDF_GLSL
#define ALPHA_COMMON_BRDF_GLSL

#include "math.glsl"

/// GGX/Trowbridge-Reitz Normal Distribution Function
/// Models the statistical distribution of microfacet normals
float NormalDistributionFunction(vec3 normal, vec3 halfVector, float roughness) {
    float a = max(roughness * roughness, 0.045 * 0.045);
    float a2 = a * a;
    float NdotH = max(dot(normal, halfVector), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom + EPSILON);
}

/// Fresnel-Schlick approximation with roughness
/// Models how reflectivity changes at grazing angles
vec3 FresnelSchlickRoughness(float VdotH, vec3 F0, float roughness) {
    float Fc = pow(1.0 - VdotH, 5.0);
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * Fc;
}

/// Schlick-GGX Geometry function (single direction)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + EPSILON);
}

/// Smith's Geometry function - combines shadowing and masking
/// Models microfacet self-shadowing from both view and light directions
float GeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness) {
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotL = max(dot(normal, lightDir), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

/// Full Cook-Torrance specular BRDF
/// Combines D (distribution), F (fresnel), G (geometry) terms
vec3 cookTorranceBRDF(vec3 normal, vec3 viewDir, vec3 lightDir, vec3 halfVector,
                      vec3 F0, float roughness, float NdotL, float NdotV) {
    float VdotH = max(dot(viewDir, halfVector), 0.0);
    float D = NormalDistributionFunction(normal, halfVector, roughness);
    vec3 F = FresnelSchlickRoughness(VdotH, F0, roughness);
    float G = GeometrySmith(normal, viewDir, lightDir, roughness);
    float denom = 4.0 * max(NdotL, 0.0) * max(NdotV, 0.0) + EPSILON;
    return (D * G * F) / denom;
}

#endif
