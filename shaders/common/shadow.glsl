// Shadow sampling shared by the opaque and transparent lighting passes: cascaded, spot and point
// shadow lookups with PCF, PCSS-lite penumbrae and interleaved gradient noise.
// Include with GL_GOOGLE_include_directive, AFTER the includer has declared:
//   directionalShadowMaps[], spotShadowMaps[], pointShadowMaps[]  (sampler arrays)
//   lightMatrices.shadowcastingLightMatrices[]                      (matrix block)
//   directionalCascadeSplits.cascadeSplits[]                        (cascade split block)
//   float shadowViewDepth(vec3 worldPos)                            (positive view-space depth)
#ifndef ALPHA_COMMON_SHADOW_GLSL
#define ALPHA_COMMON_SHADOW_GLSL

#include "light.glsl"

const float BASE_DEPTH_BIAS = 0.005;
const float MAX_SHADOW_BIAS = 0.15;

//=============================================================================
// UTILITY FUNCTIONS
//=============================================================================
// Interleaved Gradient Noise by Jorge Jimenez
// Produces structured noise that's much less visually objectionable than white noise
// and integrates well with TAA
float InterleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// Vogel disk - more uniform distribution than Poisson disk
// Generates sample points in a spiral pattern for better coverage
vec2 VogelDiskSample(int sampleIndex, int sampleCount, float rotation) {
    float goldenAngle = 2.4; // ~137.5 degrees in radians
    float r = sqrt((float(sampleIndex) + 0.5) / float(sampleCount));
    float theta = float(sampleIndex) * goldenAngle + rotation;
    return vec2(cos(theta), sin(theta)) * r;
}

//=============================================================================
// SHADOW FUNCTIONS
//=============================================================================

/// Determines which cascade a fragment belongs to based on view depth
/// Also outputs blend factor for smooth cascade transitions
/// Also outputs distance fade factor (1.0 = full shadow, 0.0 = no shadow beyond fade region)
/// Returns -1 if beyond the fade region (max shadow distance + fade margin)
int findCascade(float viewDepth, vec4 cascadeSplits, out float blendFactor, out float distanceFade) {
    // Blend region as percentage of cascade depth (10% overlap)
    const float BLEND_THRESHOLD = 0.1;
    // Distance fade extends BEYOND max shadow distance by this multiplier
    // e.g., 1.2 = fade from 100% to 120% of max distance
    const float FADE_END_MULTIPLIER = 1.2;
    
    blendFactor = 0.0;
    distanceFade = 1.0;
    
    float maxShadowDistance = cascadeSplits.w;
    float fadeEndDistance = maxShadowDistance * FADE_END_MULTIPLIER;
    
    // Beyond fade region - no shadows at all
    if (viewDepth > fadeEndDistance) {
        distanceFade = 0.0;
        return -1;
    }
    
    // Calculate distance fade AFTER max shadow distance
    if (viewDepth > maxShadowDistance) {
        // Smooth fade from 1.0 at maxShadowDistance to 0.0 at fadeEndDistance
        distanceFade = 1.0 - smoothstep(maxShadowDistance, fadeEndDistance, viewDepth);
    }
    
    if (viewDepth < cascadeSplits.x) {
        // Calculate blend factor near cascade 0 boundary
        float cascadeEnd = cascadeSplits.x;
        float blendStart = cascadeEnd * (1.0 - BLEND_THRESHOLD);
        if (viewDepth > blendStart) {
            blendFactor = (viewDepth - blendStart) / (cascadeEnd - blendStart);
        }
        return 0;
    }
    if (viewDepth < cascadeSplits.y) {
        float cascadeEnd = cascadeSplits.y;
        float blendStart = cascadeEnd * (1.0 - BLEND_THRESHOLD);
        if (viewDepth > blendStart) {
            blendFactor = (viewDepth - blendStart) / (cascadeEnd - blendStart);
        }
        return 1;
    }
    if (viewDepth < cascadeSplits.z) {
        float cascadeEnd = cascadeSplits.z;
        float blendStart = cascadeEnd * (1.0 - BLEND_THRESHOLD);
        if (viewDepth > blendStart) {
            blendFactor = (viewDepth - blendStart) / (cascadeEnd - blendStart);
        }
        return 2;
    }
    // Cascade 3: from cascadeSplits.z to cascadeSplits.w (max shadow distance)
    return 3;
}

/// Finds the appropriate cascade index for a directional light
int findCascadeForUnifiedLight(Light light, vec3 worldPos, out float blendFactor, out float distanceFade) {
    if (light.lightType != 0) {
        blendFactor = 0.0;
        distanceFade = 1.0;
        return -1;
    }
    
    int cascadeSplitsIndex = light.shadowmapIndex;
    if (cascadeSplitsIndex < 0 || cascadeSplitsIndex >= MAX_SHADOWCASTING_DIRECTIONAL) {
        blendFactor = 0.0;
        distanceFade = 1.0;
        return -1;
    }
    
    float viewDepth = shadowViewDepth(worldPos);
    vec4 cascadeSplits = directionalCascadeSplits.cascadeSplits[cascadeSplitsIndex];
    return findCascade(viewDepth, cascadeSplits, blendFactor, distanceFade);
}

/// Sample shadow for a specific cascade (internal helper)
/// Returns: shadow value (0 = fully shadowed, 1 = fully lit), or -1 if outside cascade bounds
float sampleCascadeShadow(Light light, int cascadeIndex, vec3 worldPos, vec3 normal, vec3 lightDir) {
    if (cascadeIndex < 0 || cascadeIndex >= MAX_CASCADE_COUNT) return -1.0;
    
    mat4 lightSpaceMatrix = lightMatrices.shadowcastingLightMatrices[light.lightMatrixOffset + cascadeIndex];
    vec4 shadowCoord = lightSpaceMatrix * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;

    if (BEYOND_SHADOW_FAR(shadowCoord)) return -1.0;
    
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    
    // Check if outside shadow map bounds - return -1 to signal "try next cascade"
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 || shadowCoord.y < 0.0 || shadowCoord.y > 1.0) {
        return -1.0;
    }
    
    float NdotL = dot(normal, lightDir);
    if (NdotL <= 0.0) return 0.0;
    
    float invNdotL = 1.0 - saturate(NdotL);
    // Consistent bias across cascades to avoid seams
    float bias = 0.0005 + invNdotL * BASE_DEPTH_BIAS * 0.5;
    // Scale bias by cascade texel size (larger cascades need slightly more bias)
    bias *= (1.0 + float(cascadeIndex) * 0.15);
    
    vec2 shadowMapSize = vec2(textureSize(directionalShadowMaps[light.shadowmapIndex], 0).xy);
    vec2 texelSize = 1.0 / shadowMapSize;
    
    // Filter radius scales with cascade (larger cascades = larger world area per texel)
    float radius = 1.5 + float(cascadeIndex) * 0.5;
    
    // Use IGN for structured noise that works well with TAA
    float rotation = InterleavedGradientNoise(gl_FragCoord.xy) * 2.0 * PI;
    
    // 16 samples with Vogel disk for smooth, even coverage
    const int PCF_SAMPLES = 16;
    float shadow = 0.0;
    for (int i = 0; i < PCF_SAMPLES; i++) {
        vec2 offset = VogelDiskSample(i, PCF_SAMPLES, rotation) * radius * texelSize;
        float pcfDepth = texture(directionalShadowMaps[light.shadowmapIndex], 
                                vec3(shadowCoord.xy + offset, float(cascadeIndex))).r;
        shadow += (shadowCoord.z - bias) > pcfDepth ? 0.0 : 1.0;
    }
    return shadow / float(PCF_SAMPLES);
}

/// Calculates shadow for directional lights using cascaded shadow maps
/// Uses cascade blending for smooth transitions between cascades
/// Uses distance fade to smoothly fade shadows near max shadow distance
float findShadowForCascade(Light light, int cascadeIndex, float blendFactor, float distanceFade, vec3 worldPos, vec3 normal) {
    if (cascadeIndex < 0 || cascadeIndex >= MAX_CASCADE_COUNT) return 1.0;
    
    vec3 lightDir = normalize(-light.directionAndRange.xyz);
    float NdotL = dot(normal, lightDir);
    if (NdotL <= 0.0) return 0.0;
    
    // Sample current cascade
    float shadow = sampleCascadeShadow(light, cascadeIndex, worldPos, normal, lightDir);
    
    // If current cascade failed (outside bounds), try next cascade as fallback
    if (shadow < 0.0) {
        shadow = sampleCascadeShadow(light, cascadeIndex + 1, worldPos, normal, lightDir);
        if (shadow < 0.0) {
            return 1.0; // Outside all cascades - return lit
        }
    }
    else {
        // Blend with next cascade if near boundary
        if (blendFactor > 0.0 && cascadeIndex < MAX_CASCADE_COUNT - 1) {
            float nextShadow = sampleCascadeShadow(light, cascadeIndex + 1, worldPos, normal, lightDir);
            if (nextShadow >= 0.0) {
                // Smooth blend between cascades
                shadow = mix(shadow, nextShadow, blendFactor);
            }
        }
    }
    
    // Apply distance fade: shadow fades to 1.0 (fully lit) as distanceFade approaches 0.0
    // shadow=0.0 means fully shadowed, shadow=1.0 means fully lit
    // When distanceFade=1.0 (close), keep shadow as-is
    // When distanceFade=0.0 (at max distance), return 1.0 (fully lit)
    shadow = mix(1.0, shadow, distanceFade);
    
    return shadow;
}

/// Calculates shadow for spot lights using 16-sample rotated Poisson disk PCF
/// with distance-based penumbra for realistic soft shadows
float findShadowForSpotLight(Light light, vec3 worldPos, vec3 normal) {
    if (light.lightType != 1) return 1.0;
    
    mat4 lightSpaceMatrix = lightMatrices.shadowcastingLightMatrices[light.lightMatrixOffset];
    vec4 shadowCoord = lightSpaceMatrix * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;
    
    if (BEYOND_SHADOW_FAR(shadowCoord)) return 1.0;
    
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 || shadowCoord.y < 0.0 || shadowCoord.y > 1.0) {
        return 1.0;
    }
    
    vec3 lightPos = light.positionAndData.xyz;
    vec3 toLight = lightPos - worldPos;
    float lightRange = max(light.directionAndRange.w, 0.001);
    vec3 lightDir = normalize(toLight);
    float NdotL = max(dot(normal, lightDir), 0.0);
    if (NdotL <= 0.0) return 0.0;
    
    float invNdotL = 1.0 - saturate(NdotL);
    float bias = BASE_DEPTH_BIAS + invNdotL * MAX_SHADOW_BIAS;
    float normalizedBias = bias / lightRange;
    float fragmentDepth = saturate(length(toLight) / lightRange);
    
    vec2 shadowMapSize = vec2(textureSize(spotShadowMaps[light.shadowmapIndex], 0).xy);
    vec2 texelSize = 1.0 / shadowMapSize;
    
    // Use Interleaved Gradient Noise instead of random - much less grainy!
    // This produces structured noise that's nearly invisible and works great with TAA
    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    float rotation = noise * 2.0 * PI;
    
    // =========================================================================
    // PCSS-lite: Distance-based penumbra estimation
    // Shadows get softer further from the occluder (contact hardening)
    // =========================================================================
    
    // Step 1: Blocker search - find average blocker depth using Vogel disk
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    float searchRadius = 3.0; // Texels to search for blockers
    
    const int BLOCKER_SAMPLES = 8;
    for (int i = 0; i < BLOCKER_SAMPLES; i++) {
        vec2 sampleOffset = VogelDiskSample(i, BLOCKER_SAMPLES, rotation) * searchRadius * texelSize;
        float blockerDepth = texture(spotShadowMaps[light.shadowmapIndex], shadowCoord.xy + sampleOffset).r;
        if (blockerDepth < fragmentDepth) {
            blockerSum += blockerDepth;
            blockerCount += 1.0;
        }
    }
    
    // Step 2: Calculate penumbra width based on blocker distance
    float basePenumbra = 2.5;  // Base softness in texels
    float penumbraWidth = basePenumbra;
    
    if (blockerCount > 0.0) {
        float avgBlockerDepth = blockerSum / blockerCount;
        // Penumbra grows with distance from blocker (simplified PCSS)
        float lightSourceSize = 0.04; // Simulated light size (larger = softer)
        float penumbraRatio = (fragmentDepth - avgBlockerDepth) / max(avgBlockerDepth, 0.001);
        penumbraWidth = basePenumbra + penumbraRatio * lightSourceSize * shadowMapSize.x;
        penumbraWidth = clamp(penumbraWidth, basePenumbra, 8.0);
    }
    
    // Step 3: PCF filtering using Vogel disk for better sample distribution
    // 32 samples provides much smoother results with minimal noise
    const int PCF_SAMPLES = 32;
    float shadow = 0.0;
    for (int i = 0; i < PCF_SAMPLES; i++) {
        vec2 offset = VogelDiskSample(i, PCF_SAMPLES, rotation) * penumbraWidth * texelSize;
        float pcfDepth = texture(spotShadowMaps[light.shadowmapIndex], shadowCoord.xy + offset).r;
        shadow += (fragmentDepth - normalizedBias) > pcfDepth ? 0.0 : 1.0;
    }
    return shadow / float(PCF_SAMPLES);
}

/// Calculates shadow for point lights using Vogel disk + IGN with PCSS-lite
/// Produces smooth soft shadows with distance-based penumbra
float findShadowForPointLight(Light light, vec3 worldPos, vec3 normal) {
    if (light.lightType != 2) return 1.0;
    
    vec3 lightPos = light.positionAndData.xyz;
    vec3 lightToFragment = worldPos - lightPos;
    float lightRange = max(light.directionAndRange.w, 0.001);
    float currentDistance = length(lightToFragment);
    float normalizedDistance = currentDistance / lightRange;
    
    if (normalizedDistance >= 1.0) return 0.0;
    
    vec3 lightDir = normalize(-lightToFragment);
    float NdotL = max(dot(normal, lightDir), 0.0);
    if (NdotL <= 0.0) return 0.0;
    
    // Construct tangent frame perpendicular to sample direction for cubemap sampling
    vec3 sampleDir = normalize(lightToFragment);
    vec3 tangent = abs(sampleDir.y) < 0.999 
        ? normalize(cross(sampleDir, vec3(0.0, 1.0, 0.0)))
        : normalize(cross(sampleDir, vec3(1.0, 0.0, 0.0)));
    vec3 bitangent = cross(sampleDir, tangent);
    
    // Use IGN for structured noise
    float rotation = InterleavedGradientNoise(gl_FragCoord.xy) * 2.0 * PI;
    
    // =========================================================================
    // PCSS-lite: Blocker search for distance-based penumbra
    // =========================================================================
    float searchRadius = 0.06;  // Search area for blockers
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    
    const int BLOCKER_SAMPLES = 8;
    for (int i = 0; i < BLOCKER_SAMPLES; i++) {
        vec2 diskOffset = VogelDiskSample(i, BLOCKER_SAMPLES, rotation) * searchRadius;
        vec3 sampleOffset = tangent * diskOffset.x + bitangent * diskOffset.y;
        vec3 offsetDir = normalize(sampleDir + sampleOffset);
        
        float sampledDepth = texture(pointShadowMaps[light.shadowmapIndex], offsetDir).r;
        float sampledDistance = sampledDepth * lightRange;
        
        if (sampledDistance < currentDistance) {
            blockerSum += sampledDistance;
            blockerCount += 1.0;
        }
    }
    
    // Calculate penumbra width based on blocker distance
    float basePenumbra = 0.02;
    float penumbraWidth = basePenumbra;
    
    if (blockerCount > 0.0) {
        float avgBlockerDistance = blockerSum / blockerCount;
        float lightSourceSize = 0.03;  // Virtual light size
        float penumbraRatio = (currentDistance - avgBlockerDistance) / max(avgBlockerDistance, 0.001);
        penumbraWidth = basePenumbra + penumbraRatio * lightSourceSize;
        penumbraWidth = clamp(penumbraWidth, basePenumbra, 0.12);
    } else {
        // No blockers found - fully lit
        return 1.0;
    }
    
    // =========================================================================
    // PCF filtering with Vogel disk
    // =========================================================================
    float distanceBias = normalizedDistance * BASE_DEPTH_BIAS;
    float slopeBias = sqrt(max(1.0 - NdotL * NdotL, 0.0)) / max(NdotL, 0.001);
    float bias = BASE_DEPTH_BIAS + distanceBias + min(MAX_SHADOW_BIAS * slopeBias, MAX_SHADOW_BIAS);
    
    const int PCF_SAMPLES = 16;
    float shadow = 0.0;
    for (int i = 0; i < PCF_SAMPLES; i++) {
        vec2 diskOffset = VogelDiskSample(i, PCF_SAMPLES, rotation) * penumbraWidth;
        vec3 sampleOffset = tangent * diskOffset.x + bitangent * diskOffset.y;
        vec3 offsetDir = normalize(sampleDir + sampleOffset);
        
        float sampledDistance = texture(pointShadowMaps[light.shadowmapIndex], offsetDir).r * lightRange;
        shadow += (currentDistance - bias) > sampledDistance ? 0.0 : 1.0;
    }
    
    return shadow / float(PCF_SAMPLES);
}

/// Main shadow dispatcher - routes to appropriate shadow function based on light type
float calculateShadow(Light light, vec3 worldPos, vec3 normal) {
    if (light.isCastingShadow == 0) return 1.0;

    if (light.lightType == 0) {
        float blendFactor;
        float distanceFade;
        int cascadeIndex = findCascadeForUnifiedLight(light, worldPos, blendFactor, distanceFade);
        return findShadowForCascade(light, cascadeIndex, blendFactor, distanceFade, worldPos, normal);
    } else if (light.lightType == 1) {
        return findShadowForSpotLight(light, worldPos, normal);
    } else if (light.lightType == 2) {
        return findShadowForPointLight(light, worldPos, normal);
    }
    return 1.0;
}

#endif
