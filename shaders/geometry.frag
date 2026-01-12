#version 450
//=============================================================================
// GEOMETRY PASS - G-BUFFER FILL
//=============================================================================
//
// Purpose: Render geometry into the G-Buffer for deferred shading.
// The G-Buffer stores all surface properties needed for lighting calculations.
//
// G-Buffer Contents:
//   - Position: World-space vertex positions for distance/attenuation calculations
//   - Normal: World-space surface normals for lighting dot products
//   - Albedo: Base diffuse color (RGB) and opacity (A) for alpha masking
//   - Material: Metallic (R), Smoothness (G), Ambient Occlusion (B)
//
// Why Deferred?
//   - Decouples geometry from lighting: render geometry once, light multiple times
//   - Avoids shading every pixel multiple times for each light
//   - Enables complex lighting (shadows, GI) without vertex shader overhead
//
// Material Support:
//   - Base color (albedo) maps with alpha masking
//   - Normal maps (BC5 compressed, reconstructed Z from RG)
//   - Metallic/Smoothness maps (packed into RGBA)
//   - Ambient occlusion maps
//
// Alpha Masking:
//   - Objects with transparency (windows, foliage) use alpha masking
//   - Fully transparent pixels are discarded, creating "holes"
//   - Opaque pixels write to G-Buffer, transparent pixels go to OIT pass
//
// Normal Mapping:
//   - Tangent space normals sampled from texture
//   - Transformed to world space via TBN matrix
//   - Stored in [0,1] range for efficient G-Buffer compression
//
//=============================================================================

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 4) in vec3 worldTangent;
layout(location = 5) in vec3 worldBitangent;
layout(location = 6) in vec3 worldNormal;

// G-Buffer outputs
layout(location = 0) out vec4 outPosition;  // World space position
layout(location = 1) out vec4 outNormal;    // World space normal
layout(location = 2) out vec4 outAlbedo;    // Albedo (RGB) and Opacity (A)
layout(location = 3) out vec4 outMaterial;  // Metallic (R), Smoothness (G), AO (B), unused (A)

// Material set stays at set = 2 (matches pipeline layout for geometry pass)
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
layout(set = 2, binding = 2) uniform sampler2D normalMap;
layout(set = 2, binding = 3) uniform sampler2D metallicSmoothnessMap;
layout(set = 2, binding = 4) uniform sampler2D occlusionMap;

const float EPSILON = 0.001;



vec3 calculateNormal() {
    
    if(material.hasNormalMap == 0) {
        return normalize(worldNormal)* 0.5 + 0.5;
    }
    
    // Sample and unpack normal map (BC5: only RG channels)
    vec2 normalXY = texture(normalMap, fragUV).rg;
    normalXY = normalXY * 2.0 - 1.0;
    normalXY*=material.normalStrength*0.1;
    // Reconstruct Z
    float z = sqrt(max(1.0 - dot(normalXY, normalXY), 0.0));
    vec3 tangentNormal = vec3(normalXY, z);

    // Create TBN matrix (transposed to match Unity's mul(normal, TBN))
    mat3 TBN = mat3(
        normalize(worldTangent),
        normalize(worldBitangent),
        normalize(worldNormal)
    );


    vec3 worldSpaceNormal = normalize(TBN * tangentNormal);
    return worldSpaceNormal * 0.5 + 0.5;
}


void main() {

    vec4 albedoSample = material.albedoColor;
    if(material.hasAlbedoMap == 1) {
        albedoSample *= texture(albedoMap, fragUV);
    }
    
    if (material.isMasked == 1 && albedoSample.a < (material.alphaCutoff)) {
        discard;
    }

    float metallic = material.metallic;
    float smoothness = material.smoothness;
    if(material.hasMetallicSmoothnessMap == 1) {
        metallic *= texture(metallicSmoothnessMap, fragUV).r;
        smoothness *= texture(metallicSmoothnessMap, fragUV).a;
    }
    
    float occlusion = 1.0;
    if (material.hasOcclusionMap == 1) {
         float occlusionSample = texture(occlusionMap, fragUV).r;
         occlusion = mix(1.0, occlusionSample, material.ao);
    }

    outNormal = vec4(calculateNormal(), 1.0);
    outPosition = vec4(fragPosition, 1.0);
    outAlbedo = albedoSample;
    outMaterial = vec4(metallic, smoothness, occlusion, 1.0);
}