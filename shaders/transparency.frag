#version 450
#extension GL_GOOGLE_include_directive : require
//=============================================================================
// WEIGHTED BLENDED ORDER-INDEPENDENT TRANSPARENCY
//=============================================================================
//
// Technique: Weighted Blended OIT (McGuire & Bavoil, JCGT 2013)
//            "Weighted Blended Order-Independent Transparency"
//
// Problem Solved:
//   Traditional alpha blending requires objects to be sorted back-to-front,
//   which is expensive and breaks with intersecting geometry. OIT removes
//   this requirement by accumulating transparency in a order-independent way.
//
// How It Works:
//   1. ACCUMULATION PASS (this shader):
//      - Render all transparent objects (no sorting needed)
//      - Output 1: accum.rgb = Σ(color_i × alpha_i × weight_i)
//                  accum.a   = Σ(alpha_i × weight_i)
//      - Output 2: reveal = Π(1 - alpha_i)  (product of transmittances)
//
//   2. COMPOSITE PASS (separate fullscreen shader):
//      - final = accum.rgb / max(accum.a, epsilon)
//      - blend with background using reveal factor
//
// Weight Function:
//   weight(z, alpha) = alpha × clamp(scale / (epsilon + z^4), minW, maxW)
//
//   - Depth-based: closer objects contribute more (higher weight)
//   - Alpha-scaled: more opaque objects contribute more
//   - The z^4 term creates steep depth falloff (contact-sensitive blending)
//
// Trade-offs:
//   + No sorting required
//   + Handles intersecting geometry correctly
//   + Single pass for all transparent objects
//   - Approximate (not physically exact order)
//   - Weight function tuning needed per scene depth range
//
// References:
//   - McGuire & Bavoil, "Weighted Blended OIT", JCGT 2013
//   - http://casual-effects.blogspot.com/2015/03/weighted-blended-oit.html
//
//=============================================================================

#include "light.glsl"
#include "brdf.glsl"

const float BASE_AMBIENT_INTENSITY = 0.05;
const float BASE_DEPTH_BIAS = 0.005;
const float MAX_SHADOW_BIAS = 0.1;

// Inputs from vertex shader
layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

// Set 0: Camera UBO (shared with vertex shader)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 clipPlanes;
} camera;

// Set 1: Unified light array
layout(set = 1, binding = 0) uniform LightUbo {
    Light lights[MAX_LIGHTS];
    int lightCount;
} unifiedLights;

// Set 2: Shadow map samplers
layout(set = 2, binding = 0) uniform sampler2DArray directionalShadowMaps[MAX_SHADOWCASTING_DIRECTIONAL];
layout(set = 2, binding = 1) uniform sampler2D spotShadowMaps[MAX_SHADOWCASTING_SPOT];
layout(set = 2, binding = 2) uniform samplerCube pointShadowMaps[MAX_SHADOWCASTING_POINT];

// Set 3: Model matrices (used by vertex shader)
// Set 4: Material uniforms and textures
layout(set = 4, binding = 0) uniform MaterialUbo {
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

layout(set = 4, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 4, binding = 2) uniform sampler2D normalTexture;
layout(set = 4, binding = 3) uniform sampler2D metallicSmoothnessTexture;
layout(set = 4, binding = 4) uniform sampler2D occlusionTexture;

// Set 5: Scene lighting (must match Rendering::SceneLightingUbo)
layout(set = 5, binding = 0) uniform SceneLightingUbo {
    mat4 viewMatrix;
    mat4 projectionMatrix;
    vec4 cameraPosition;
    float ambientIntensity;
    float reflectionIntensity;
} sceneLighting;

// Set 6: Shadow light matrices
layout(set = 6, binding = 0) uniform ShadowcastingLightMatrices {
    mat4 shadowcastingLightMatrices[MAX_SHADOWCASTING_LIGHT_MATRICES];
} lightMatrices;

// Set 7: Cascade splits
layout(set = 7, binding = 0) uniform DirectionalLightCascadeSplits {
    vec4 cascadeSplits[MAX_SHADOWCASTING_DIRECTIONAL];
} directionalCascadeSplits;

// OIT outputs (weighted blended algorithm)
layout(location = 0) out vec4 accum;   // RGB = Color*weight, A = weight
layout(location = 1) out float reveal; // Used to modulate the transparency weight

// ----- PBR FUNCTIONS -----
vec3 calculateNormal() {
    vec3 N = normalize(fragNormal);
    
    vec3 normalMapValue = texture(normalTexture, fragUV).xyz;
    
    if (normalMapValue == vec3(0.5, 0.5, 1.0) || length(normalMapValue) < 0.1) {
        return N;
    }
    
    vec3 tangentSpaceNormal = normalMapValue * 2.0 - 1.0;
    
    mat3 TBN = mat3(
        normalize(fragTangent),
        normalize(fragBitangent),
        N
    );
    
    return normalize(TBN * tangentSpaceNormal);
}

// ----- PSEUDO-RANDOM FUNCTIONS -----
float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

// Poisson disk sampling for better shadow quality
vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870),
    vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845),
    vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507),
    vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790)
);

// ----- SHADOW CALCULATIONS (matching direct_light.frag) -----
int findCascade(float viewDepth, vec4 cascadeSplits) {
    if (viewDepth < cascadeSplits.x) return 0;
    if (viewDepth < cascadeSplits.y) return 1;
    if (viewDepth < cascadeSplits.z) return 2;
    return 3;
}

int findCascadeForUnifiedLight(Light light, vec3 worldPos) {
    if (light.lightType != 0) {
        return -1;
    }
    
    int cascadeSplitsIndex = light.lightMatrixOffset / MAX_CASCADE_COUNT;
    
    if (cascadeSplitsIndex < 0 || cascadeSplitsIndex >= MAX_SHADOWCASTING_LIGHT_MATRICES) {
        return -1;
    }
    
    float viewDepth = abs((sceneLighting.viewMatrix * vec4(worldPos, 1.0)).z);
    vec4 cascadeSplits = directionalCascadeSplits.cascadeSplits[cascadeSplitsIndex];
    
    return findCascade(viewDepth, cascadeSplits);
}

float findShadowForCascade(Light light, int cascadeIndex, vec3 worldPos, vec3 normal) {
    if (cascadeIndex < 0 || cascadeIndex >= MAX_CASCADE_COUNT) {
        return 1.0;
    }
    
    mat4 lightSpaceMatrix = lightMatrices.shadowcastingLightMatrices[light.lightMatrixOffset + cascadeIndex];
    vec4 shadowCoord = lightSpaceMatrix * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;
    
    if (BEYOND_SHADOW_FAR(shadowCoord)) {
        return 1.0;
    }
    
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 || 
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0) {
        return 1.0;
    }
    
    vec3 lightDir = normalize(-light.directionAndRange.xyz);
    float NdotL = dot(normal, lightDir);
    
    if (NdotL <= 0.0) {
        return 0.0;
    }
    
    float invNdotL = 1.0 - saturate(NdotL);
    float normalBias = invNdotL * BASE_DEPTH_BIAS;
    float depthBias = 0.001;
    float bias = depthBias + normalBias;
    bias *= (1.0 + float(cascadeIndex) * 0.3);
    
    vec2 texelSize = 1.0 / vec2(textureSize(directionalShadowMaps[light.shadowmapIndex], 0).xy);
    float radius = 1.0 + float(cascadeIndex) * 0.5;
    
    vec2 screenPos = gl_FragCoord.xy;
    float rotation = rand(screenPos) * 2.0 * PI;
    mat2 rotationMatrix = mat2(
        cos(rotation), -sin(rotation),
        sin(rotation), cos(rotation)
    );
    
    float shadow = 0.0;
    for(int i = 0; i < 16; i++) {
        vec2 offset = rotationMatrix * poissonDisk[i] * radius * texelSize;
        float pcfDepth = texture(directionalShadowMaps[light.shadowmapIndex], 
                                vec3(shadowCoord.xy + offset, float(cascadeIndex))).r;
        shadow += (shadowCoord.z - bias) > pcfDepth ? 0.0 : 1.0;
    }
    
    shadow /= 16.0;
    return shadow;
}

float findShadowForSpotLight(Light light, vec3 worldPos, vec3 normal) {
    if (light.lightType != 1) {
        return 1.0;
    }
    
    mat4 lightSpaceMatrix = lightMatrices.shadowcastingLightMatrices[light.lightMatrixOffset];
    vec4 shadowCoord = lightSpaceMatrix * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;
    
    if (BEYOND_SHADOW_FAR(shadowCoord)) {
        return 1.0;
    }
    
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 || 
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0) {
        return 1.0;
    }
    
    vec3 lightPos = light.positionAndData.xyz;
    vec3 toLight = lightPos - worldPos;
    vec3 lightDir = normalize(toLight);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    if (NdotL <= 0.0) {
        return 0.0;
    }
    
    // The spot shadow map stores radial distance / range (see shadowmap.frag), not NDC depth.
    float lightRange = max(light.directionAndRange.w, 0.001);
    float invNdotL = 1.0 - saturate(NdotL);
    float bias = BASE_DEPTH_BIAS + invNdotL * MAX_SHADOW_BIAS;
    float normalizedBias = bias / lightRange;
    float fragmentDepth = saturate(length(toLight) / lightRange);
    
    vec2 texelSize = 1.0 / vec2(textureSize(spotShadowMaps[light.shadowmapIndex], 0).xy);
    float radius = 1.5;
    
    vec2 screenPos = gl_FragCoord.xy;
    float rotation = rand(screenPos) * 2.0 * PI;
    mat2 rotationMatrix = mat2(
        cos(rotation), -sin(rotation),
        sin(rotation), cos(rotation)
    );
    
    float shadow = 0.0;
    for(int i = 0; i < 16; i++) {
        vec2 offset = rotationMatrix * poissonDisk[i] * radius * texelSize;
        float pcfDepth = texture(spotShadowMaps[light.shadowmapIndex], shadowCoord.xy + offset).r;
        shadow += (fragmentDepth - normalizedBias) > pcfDepth ? 0.0 : 1.0;
    }
    
    shadow /= 16.0;
    return shadow;
}

float findShadowForPointLight(Light light, vec3 worldPos, vec3 normal) {
    if (light.lightType != 2) {
        return 1.0;
    }
    
    vec3 lightPos = light.positionAndData.xyz;
    vec3 lightToFragment = worldPos - lightPos;
    float lightRange = max(light.directionAndRange.w, 0.001);
    
    float currentDistance = length(lightToFragment);
    float normalizedDistance = currentDistance / lightRange;
    
    if (normalizedDistance >= 1.0) {
        return 0.0;
    }
    
    vec3 lightDir = normalize(-lightToFragment);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    if (NdotL <= 0.0) {
        return 0.0;
    }
    
    float distanceBias = normalizedDistance * BASE_DEPTH_BIAS;
    float slopeBias = sqrt(max(1.0 - NdotL * NdotL, 0.0)) / max(NdotL, 0.001);
    float bias = BASE_DEPTH_BIAS + distanceBias + min(MAX_SHADOW_BIAS * slopeBias, MAX_SHADOW_BIAS);
    
    float shadow = 0.0;
    float samples = 0.0;
    
    vec3 sampleOffsets[4] = vec3[](
        vec3(0.01, 0.01, 0.0),
        vec3(-0.01, 0.01, 0.0),
        vec3(0.01, -0.01, 0.0),
        vec3(-0.01, -0.01, 0.0)
    );
    
    for(int i = 0; i < 4; i++) {
        vec3 sampleDir = normalize(lightToFragment + sampleOffsets[i]);
        float sampledDistance = texture(pointShadowMaps[light.shadowmapIndex], sampleDir).r;
        float sampledWorldDistance = sampledDistance * lightRange;
        shadow += (currentDistance - bias) > sampledWorldDistance ? 0.0 : 1.0;
        samples += 1.0;
    }
    
    float primarySample = texture(pointShadowMaps[light.shadowmapIndex], lightToFragment).r;
    float primaryWorldDistance = primarySample * lightRange;
    shadow += (currentDistance - bias) > primaryWorldDistance ? 0.0 : 1.0;
    samples += 1.0;
    
    shadow /= samples;
    return shadow;
}

float calculateShadow(Light light, vec3 worldPos, vec3 normal) {
    if (light.isCastingShadow == 0)
        return 1.0;
    
    if (light.lightType == 0) {
        return findShadowForCascade(light, findCascadeForUnifiedLight(light, worldPos), worldPos, normal);
    } else if (light.lightType == 1) {
        return findShadowForSpotLight(light, worldPos, normal);
    } else if (light.lightType == 2) {
        return findShadowForPointLight(light, worldPos, normal);
    }
    
    return 1.0;
}

// ----- ATTENUATION CALCULATIONS (matching direct_light.frag) -----
float DistanceAttenuation(float distanceSqr, vec2 distanceAndRangeSqr) {
    float lightAtten = 1.0 / max(distanceSqr, EPSILON);
    float factor = distanceSqr * distanceAndRangeSqr.x;
    float smoothFactor = saturate(1.0 - factor * factor);
    smoothFactor = smoothFactor * smoothFactor;
    
    float hasRange = step(EPSILON, distanceAndRangeSqr.x);
    return mix(1.0, lightAtten * smoothFactor, hasRange);
}

// ----- UNIFIED LIGHT CALCULATION -----
vec3 calculateUnifiedLight(
    Light light,
    vec3 worldPos,
    vec3 normal,
    vec3 viewDir,
    vec3 albedo,
    float roughness,
    float metallic,
    vec3 F0,
    vec3 kS,
    vec3 kD
) {
    // Unity's unified light vector calculation
    vec3 lightVector = light.positionAndData.xyz - worldPos * light.positionAndData.w;
    float distanceSqr = max(dot(lightVector, lightVector), EPSILON);
    
    vec3 lightDirection = lightVector * inversesqrt(distanceSqr);
    
    vec2 distanceAndRange = vec2(light.attenuationParams.x, light.attenuationParams.y);
    float distanceAttenuation = DistanceAttenuation(distanceSqr, distanceAndRange);
    
    vec2 spotParams = light.attenuationParams.zw;
    float angleAttenuation = AngleAttenuation(-light.directionAndRange.xyz, lightDirection, spotParams);
    float attenuation = distanceAttenuation * angleAttenuation;
    
    float NdotL = max(dot(normal, lightDirection), 0.0);
    if (NdotL <= 0.0 || attenuation <= 0.0) {
        return vec3(0.0);
    }
    
    float shadow = calculateShadow(light, worldPos, normal);
    float NdotV = max(dot(normal, viewDir), 0.0);
    vec3 halfVector = normalize(lightDirection + viewDir);
    
    vec3 diffuse = kD * albedo / PI;
    vec3 specular = cookTorranceBRDF(normal, viewDir, lightDirection, halfVector, F0, roughness, NdotL, NdotV);
    
    vec3 lightColor = light.colorAndIntensity.rgb * light.colorAndIntensity.a;
    
    return (diffuse + specular) * lightColor * NdotL * attenuation * shadow;
}

// ----- OIT WEIGHT CALCULATION -----
// Depth-only weight (McGuire & Bavoil). Alpha is applied by the caller, once, in the accumulation write.
// maxWeight bounds the weight itself so the RGBA16F accumulation target cannot overflow for bright HDR colors.
float calculateWeight(float viewDepth) {
    float z = abs(viewDepth);
    
    float a = 10.0;
    float minWeight = 1e-2;
    float maxWeight = 3e3;
    float depthScale = 200.0;
    
    float weight = a * 0.03 / (1e-5 + pow(z / depthScale, 4.0));
    return clamp(weight, minWeight, maxWeight);
}

// ----- MAIN -----
void main() {
    // Sample material textures
    vec4 baseColor = texture(baseColorTexture, fragUV) * material.albedoColor;
    
    // Discard fully transparent pixels
    if (baseColor.a < 0.01) {
        discard;
    }
    
    // Get normal
    vec3 N = calculateNormal();
    
    // Get metallic and roughness
    vec4 metallicSmoothness = texture(metallicSmoothnessTexture, fragUV);
    float metallic = metallicSmoothness.r * material.metallic;
    float smoothness = metallicSmoothness.a * material.smoothness;
    float roughness = max(1.0 - smoothness, 0.045);
    
    // Calculate view direction
    vec3 V = normalize(camera.cameraPosition.xyz - fragPosition);
    
    // Calculate F0 and energy conservation
    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    // Accumulate direct lighting
    vec3 directLighting = vec3(0.0);
    for (int i = 0; i < unifiedLights.lightCount; ++i) {
        directLighting += calculateUnifiedLight(
            unifiedLights.lights[i], fragPosition, N, V,
            baseColor.rgb, roughness, metallic, F0, kS, kD
        );
    }
    
    // Indirect lighting (ambient)
    float occlusion = material.ao;
    if (material.hasOcclusionMap == 1) {
        occlusion *= texture(occlusionTexture, fragUV).r;
    }

    vec3 indirectDiffuse = vec3(sceneLighting.ambientIntensity) * baseColor.rgb * BASE_AMBIENT_INTENSITY * occlusion;
    vec3 indirectLighting = kD * indirectDiffuse;
    
    // Final color
    vec3 finalColor = directLighting + indirectLighting;
    
    // Calculate OIT weight
    float viewDepth = (camera.view * vec4(fragPosition, 1.0)).z;
    float weight = calculateWeight(viewDepth);
    
    // Weighted blend accumulation: rgb = color * alpha * w, a = alpha * w
    accum = vec4(finalColor * baseColor.a, baseColor.a) * weight;
    
    // Revealage factor
    reveal = baseColor.a;
} 