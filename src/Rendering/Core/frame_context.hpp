#pragma once

#include "Math/AABB.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/RenderPasses/Shadowmapping/shadow_map.hpp"
#include "ECS/ecs_types.hpp"
#include "core.hpp"
#include "ECS/ecs.hpp"
#include "Rendering/Resources/material.hpp"
#include "Math/view_frustum.hpp"
#include <array>
using namespace ECS;
using namespace Math;
namespace Rendering {

// Maps for instanced rendering - use mesh pointer as key along with material
struct MeshMaterialSubmeshKey {
    Mesh* mesh = nullptr;
    Material* material = nullptr;
    uint32_t submeshIndex = 0;

    bool operator==(const MeshMaterialSubmeshKey& other) const {
        return mesh == other.mesh && material == other.material && submeshIndex == other.submeshIndex;
    }
};

} // namespace Rendering

namespace std {
template <> struct hash<Rendering::MeshMaterialSubmeshKey> {
    std::size_t operator()(const Rendering::MeshMaterialSubmeshKey& key) const {
        std::size_t h1 = std::hash<Mesh*>{}(key.mesh);
        std::size_t h2 = std::hash<Material*>{}(key.material);
        std::size_t h3 = std::hash<uint32_t>{}(key.submeshIndex);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

} // namespace std

namespace Rendering {

struct LightData {
    std::vector<SpotLight*> spotLights;
    std::vector<PointLight*> pointLights;
    std::vector<DirectionalLight*> directionalLights;
};

struct ShadowcastingData {
    // Per-light storage of model matrices to keep cascades/faces independent
    std::unordered_map<DirectionalLight*, std::array<std::unordered_map<MeshMaterialSubmeshKey, std::vector<glm::mat4>>,
                                                     MAX_SHADOW_CASCADE_COUNT>>
        directionalShadowModelsByCascade;
    std::unordered_map<SpotLight*, std::unordered_map<MeshMaterialSubmeshKey, std::vector<glm::mat4>>> spotShadowModels;
    std::unordered_map<PointLight*, std::array<std::unordered_map<MeshMaterialSubmeshKey, std::vector<glm::mat4>>, 6>>
        pointShadowModelsByFace;

    std::unordered_map<DirectionalLight*, std::array<std::vector<MeshMaterialSubmeshKey>, MAX_SHADOW_CASCADE_COUNT>>
        directionalShadowcastingKeyMapByCascade;
    std::unordered_map<SpotLight*, std::vector<MeshMaterialSubmeshKey>> spotShadowcastingKeyMap;
    std::unordered_map<PointLight*, std::array<std::vector<MeshMaterialSubmeshKey>, 6>> pointShadowcastingKeyMapByFace;
    uint32_t directionalShadowCastingCount = 0;
    uint32_t spotShadowCastingCount = 0;
    uint32_t pointShadowCastingCount = 0;
};

struct MaterialBatch {
    Material* material = nullptr;
    Mesh* mesh = nullptr;
    uint32_t submeshIndex = 0;
    uint32_t matrixOffset = 0;
    uint32_t instanceCount = 0;
};

struct MeshRenderingData {
    std::unordered_map<MeshMaterialSubmeshKey, std::vector<glm::mat4>> opaqueModelMap;
    std::unordered_map<MeshMaterialSubmeshKey, std::vector<glm::mat4>> opaqueNormalMap;
    std::unordered_map<MeshMaterialSubmeshKey, std::vector<glm::mat4>> transparentModelMap;
    std::unordered_map<MeshMaterialSubmeshKey, std::vector<glm::mat4>> transparentNormalMap;
    uint32_t opaqueInstanceCount = 0;
    uint32_t transparentInstanceCount = 0;
};

struct CameraData {
    ViewFrustum viewFrustum{};
    glm::mat4 viewProjectionMatrix{1.0f};
    glm::mat4 viewMatrix{1.0f};
    glm::mat4 invViewMatrix{1.0f};
    glm::mat4 invProjectionMatrix{1.0f};
    glm::mat4 projectionMatrix{1.0f};
    glm::vec3 position{};
    float fov = 0.0f;
    float aspectRatio = 0.0f;
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
};

// Previous frame camera data for temporal reprojection
struct PrevCameraData {
    glm::mat4 viewProjectionMatrix{1.0f};
    glm::mat4 invViewProjectionMatrix{1.0f};
};

struct FrameContext {
    // === CORE FRAME DATA ===
    uint32_t frameIndex = 0; // frame-in-flight slot: index for all per-frame resources
    uint32_t imageIndex = 0; // acquired swapchain image: index only for resources that target the swapchain
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkExtent2D extent{};
    float frameTime = 0.0f;

    VkDescriptorSet cameraDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet modelsDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet gBufferDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet lightArrayDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet cascadeSplitsDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet sceneLightingDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet lightMatrixDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet shadowModelMatrixDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet shadowMapSamplerDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet transparencyModelDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet compositionDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet depthPyramidDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet rcBuildDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet rcResolveDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet smaaEdgeDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet smaaWeightDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet smaaBlendDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet colorCorrectionDescriptorSet = VK_NULL_HANDLE;

    Buffer* cameraUniformBuffer = nullptr;
    Buffer* modelMatrixBuffer = nullptr;
    Buffer* normalMatrixBuffer = nullptr;
    Buffer* lightArrayUniformBuffer = nullptr;
    Buffer* cascadeSplitsBuffer = nullptr;
    Buffer* sceneLightingBuffer = nullptr;
    Buffer* lightMatrixBuffer = nullptr;
    Buffer* shadowModelMatrixBuffer = nullptr;
    Buffer* transparencyModelMatrixBuffer = nullptr;
    Buffer* transparencyNormalMatrixBuffer = nullptr;

    VkImageView depthView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;

    // Depth pyramid (sampled for screenspace raymarching)
    VkImageView depthPyramidView = VK_NULL_HANDLE;
    VkImage depthPyramidImage = VK_NULL_HANDLE;
    uint32_t depthPyramidMipLevels = 0;
    std::vector<VkImageView> depthPyramidMipStorageViews;
    std::vector<VkDescriptorSet> depthPyramidMipDescriptorSets;

    VkImageView lightPassResultView = VK_NULL_HANDLE;
    VkSampler lightPassSampler = VK_NULL_HANDLE;
    VkImageView lightIncidentView = VK_NULL_HANDLE;

    VkImageView accumulationView = VK_NULL_HANDLE;
    VkImageView revealageView = VK_NULL_HANDLE;

    // Indirect GI buffer
    VkImageView giIndirectView = VK_NULL_HANDLE;
    VkImage giIndirectImage = VK_NULL_HANDLE;

    // Post-process render targets
    VkImageView compositionColorView = VK_NULL_HANDLE;
    VkImage compositionColorImage = VK_NULL_HANDLE;
    VkImageView smaaEdgeView = VK_NULL_HANDLE;
    VkImage smaaEdgeImage = VK_NULL_HANDLE;
    VkImageView smaaBlendView = VK_NULL_HANDLE;
    VkImage smaaBlendImage = VK_NULL_HANDLE;
    VkImageView postAAColorView = VK_NULL_HANDLE;
    VkImage postAAColorImage = VK_NULL_HANDLE;

    // GI history for temporal accumulation (previous frame's GI output)
    VkImageView giHistoryView = VK_NULL_HANDLE;
    VkSampler giHistorySampler = VK_NULL_HANDLE;

    // Previous frame camera data for motion vector computation
    PrevCameraData prevCameraData{};

    // Frame counter for temporal jittering
    uint32_t temporalFrameIndex = 0;

    VkImageView gBufferPositionView = VK_NULL_HANDLE;
    VkImageView gBufferNormalView = VK_NULL_HANDLE;
    VkImageView gBufferAlbedoView = VK_NULL_HANDLE;
    VkImageView gBufferMaterialView = VK_NULL_HANDLE;

    // RC per-cascade atlas views for this frame
    std::array<VkImageView, RC_CASCADE_COUNT> rcRadianceViews{};

    VkImage gBufferPositionImage = VK_NULL_HANDLE;
    VkImage gBufferNormalImage = VK_NULL_HANDLE;
    VkImage gBufferAlbedoImage = VK_NULL_HANDLE;
    VkImage gbufferMaterialImage = VK_NULL_HANDLE;

    // Shadow map references for this frame
    std::array<ShadowMap*, MAX_DIRECTIONAL_LIGHTS> directionalShadowMaps{};
    std::array<ShadowMap*, MAX_SPOT_LIGHTS> spotShadowMaps{};
    std::array<ShadowMap*, MAX_POINT_LIGHTS> pointShadowMaps{};

    CameraData cameraData{};
    std::array<MaterialBatch, BASE_INSTANCED_RENDERABLES> opaqueMaterialBatches{};
    uint32_t opaqueMaterialBatchCount = 0;

    std::array<MaterialBatch, BASE_INSTANCED_RENDERABLES> transparentMaterialBatches{};
    uint32_t transparentMaterialBatchCount = 0;

    std::unordered_map<DirectionalLight*, std::array<std::vector<MaterialBatch>, MAX_SHADOW_CASCADE_COUNT>>
        directionalShadowcastingMaterialMap;
    std::unordered_map<SpotLight*, std::vector<MaterialBatch>> spotShadowcastingMaterialMap;
    std::unordered_map<PointLight*, std::array<std::vector<MaterialBatch>, 6>> pointShadowcastingMaterialMapByFace;

    // Matrix base indices in lightMatrixBuffer for each light type
    std::unordered_map<DirectionalLight*, uint32_t> directionalLightMatrixBase;
    std::unordered_map<SpotLight*, uint32_t> spotLightMatrixBase;
    std::unordered_map<PointLight*, uint32_t> pointLightMatrixBase;
};

} // namespace Rendering