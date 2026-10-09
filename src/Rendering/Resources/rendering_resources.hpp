#pragma once

// lib
#include "core.hpp"
#include "Rendering/Core/device.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/Core/buffer.hpp"
#include "Rendering/Resources/frame_image.hpp"
#include "Rendering/Resources/gbuffer.hpp"
#include "Rendering/Core/descriptors.hpp"
#include "Rendering/rendering_constants.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/RenderPasses/Shadowmapping/shadow_map.hpp"
#include "Rendering/Resources/texture.hpp"
#include "Rendering/Core/frame_context.hpp"

namespace Rendering {

// Central registry and lifetime owner of GPU resources
class RenderingResources {
public:
    // materialDescriptorSetLayout is the layout every material set is allocated from. The ResourceManager owns it
    // (materials load before this object exists); it must outlive this object.
    RenderingResources(Device& device, SwapChain& swapChain, VkDescriptorSetLayout materialDescriptorSetLayout);
    ~RenderingResources();

    // Non-copyable
    RenderingResources(const RenderingResources&) = delete;
    RenderingResources& operator=(const RenderingResources&) = delete;

    void cleanup();

    // Accessor methods for formats and resources
    VkFormat getDepthFormat() const { return depthFormat; }
    VkFormat getPositionFormat() const { return positionFormat; }
    VkFormat getNormalFormat() const { return normalFormat; }
    VkFormat getAlbedoFormat() const { return albedoFormat; }
    VkFormat getMaterialFormat() const { return materialFormat; }
    VkFormat getRevealageFormat() const { return revealageFormat; }
    VkFormat getHDRFormat() const { return hdrFormat; }
    VkFormat getDepthPyramidFormat() const { return depthPyramidFormat; }
    VkFormat getPostProcessFormat() const { return postProcessFormat; }
    VkFormat getSMAAEdgeFormat() const { return smaaEdgeFormat; }
    VkFormat getSMAABlendFormat() const { return smaaBlendFormat; }

    VkDescriptorSetLayout getCameraDescriptorSetLayout() const { return cameraDescriptorSetLayout; }
    VkDescriptorSetLayout getModelsDescriptorSetLayout() const { return modelsDescriptorSetLayout; }
    VkDescriptorSetLayout getMaterialDescriptorSetLayout() const { return materialDescriptorSetLayout; }
    VkDescriptorSetLayout getGBufferDescriptorSetLayout() const { return gBufferDescriptorSetLayout; }
    VkDescriptorSetLayout getLightArrayDescriptorSetLayout() const { return lightArrayDescriptorSetLayout; }
    VkDescriptorSetLayout getCascadeSplitsDescriptorSetLayout() const { return cascadeSplitsSetLayout; }
    VkDescriptorSetLayout getSceneLightingDescriptorSetLayout() const { return sceneLightingDescriptorSetLayout; }
    VkDescriptorSetLayout getShadowSamplerDescriptorSetLayout() const { return shadowMapSamplerLayout; }
    VkDescriptorSetLayout getShadowcastingLightMatrixDescriptorSetLayout() const {
        return shadowcastinglightMatrixDescriptorSetLayout;
    }
    VkDescriptorSetLayout getShadowModelMatrixDescriptorSetLayout() const {
        return shadowModelMatrixDescriptorSetLayout;
    }
    VkDescriptorSetLayout getEnvironmentalReflectionsDescriptorSetLayout() const { return skyboxDescriptorSetLayout; }
    VkDescriptorSetLayout getSkyboxDescriptorSetLayout() const { return skyboxDescriptorSetLayout; }
    VkDescriptorSetLayout getTransparencyModelDescriptorSetLayout() const {
        return transparencyModelDescriptorSetLayout;
    }
    VkDescriptorSetLayout getCompositionDescriptorSetLayout() const { return compositionSetLayout; }
    VkDescriptorSetLayout getRCBuildDescriptorSetLayout() const { return rcBuildSetLayout; }
    VkDescriptorSetLayout getRCResolveDescriptorSetLayout() const { return rcResolveSetLayout; }
    VkDescriptorSetLayout getDepthPyramidDescriptorSetLayout() const { return depthPyramidSetLayout; }
    // Post-processing layouts
    VkDescriptorSetLayout getSMAAEdgeSetLayout() const { return smaaEdgeSetLayout; }
    VkDescriptorSetLayout getSMAAWeightSetLayout() const { return smaaWeightSetLayout; }
    VkDescriptorSetLayout getSMAABlendSetLayout() const { return smaaBlendSetLayout; }
    VkDescriptorSetLayout getColorCorrectionSetLayout() const { return colorCorrectionSetLayout; }
    VkSampler getPostProcessSampler() const { return postProcessSampler; }

    //Shadow map accessors - now with frame index support
    ShadowMap& getDirectionalShadowMap(size_t lightIndex, size_t frameIndex) {
        return *directionalMaps[lightIndex][frameIndex];
    }
    ShadowMap& getSpotShadowMap(size_t lightIndex, size_t frameIndex) { return *spotlightMaps[lightIndex][frameIndex]; }
    ShadowMap& getPointShadowMap(size_t lightIndex, size_t frameIndex) {
        return *pointlightMaps[lightIndex][frameIndex];
    }

    // Shadow map sampler descriptor set accessor
    VkDescriptorSet getShadowMapSamplerDescriptorSet(size_t frameIndex) { return shadowMapSamplerSets[frameIndex]; }

    // GBuffer accessor
    GBuffer& getGBuffer() { return *gBuffer; }
    using FrameViews = std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>;
    const FrameViews& getDepthViews() const { return depthImage->views(); }
    const FrameViews& getLightPassResultViews() const { return lightPassResultImage->views(); }
    const FrameViews& getLightIncidentViews() const { return lightIncidentImage->views(); }
    const FrameViews& getAlbedoViews() const { return gBuffer->getAlbedoViews(); }
    const FrameViews& getAccumulationViews() const { return accumulationImage->views(); }
    const FrameViews& getRevealageViews() const { return revealageImage->views(); }
    const FrameViews& getCompositionColorViews() const { return compositionColorImage->views(); }
    const FrameViews& getSMAAEdgeViews() const { return smaaEdgeImage->views(); }
    const FrameViews& getSMAABlendViews() const { return smaaBlendImage->views(); }
    const FrameViews& getPostAAColorViews() const { return postAAColorImage->views(); }

    auto& getDirectionalLightMaps() { return directionalMaps; }
    auto& getPointLightMaps() { return pointlightMaps; }
    auto& getSpotLightMaps() { return spotlightMaps; }

    // Skybox update method
    void updateSkyboxDescriptorSet(VkImageView skyboxImageView, VkSampler skyboxSampler);
    void initializeSkyboxFromScene();

    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> createFrameContexts();

private:
    void findResourcesFormats();
    void createDepthResources();
    void createDepthPyramidResources();
    void createLightPassResources();
    void createShadowMapResources();
    void createDescriptorPool(const DescriptorBudget& budget);
    void createBuffers();
    // Creates the layouts and returns what the pool has to hold for the sets that allocateDescriptorSets allocates.
    DescriptorBudget createDescriptorSetLayouts();
    // Allocates every set, one family at a time in layout order; the write* functions fill them, one per consumer.
    void allocateDescriptorSets();
    void writeBufferSets();
    void writeGBufferSets();
    void writeShadowMapSamplerSets();
    void writeCompositionSets();
    void writePostProcessSets();
    void writeDepthPyramidSets();
    void writeRCSets();
    void createTransparencyResources();
    void createGIResources();
    void createRCAtlases();
    void createPostProcessResources();
    void loadSMAALUTTextures();

    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frameContexts;

    Device& device;
    SwapChain& swapChain;
    std::unique_ptr<GBuffer> gBuffer;
    uint32_t width;
    uint32_t height;
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    VkFormat positionFormat{VK_FORMAT_UNDEFINED};
    VkFormat normalFormat{VK_FORMAT_UNDEFINED};
    VkFormat albedoFormat{VK_FORMAT_UNDEFINED};
    VkFormat materialFormat{VK_FORMAT_UNDEFINED};
    VkFormat revealageFormat{VK_FORMAT_UNDEFINED};
    VkFormat hdrFormat{VK_FORMAT_UNDEFINED};
    VkFormat giIndirectFormat{VK_FORMAT_UNDEFINED};
    VkFormat depthPyramidFormat{VK_FORMAT_UNDEFINED};
    VkFormat postProcessFormat{VK_FORMAT_UNDEFINED}; // HDR post-AA chain format
    VkFormat smaaEdgeFormat{VK_FORMAT_R8G8_UNORM};
    VkFormat smaaBlendFormat{VK_FORMAT_R8G8B8A8_UNORM};

    std::unique_ptr<DescriptorPool> descriptorPool{nullptr};

    // Per-frame images
    std::unique_ptr<FrameImage> depthImage;
    std::unique_ptr<FrameImage> depthPyramidImage; // linearized depth, mip chain, per-mip views
    std::unique_ptr<FrameImage> lightPassResultImage;
    std::unique_ptr<FrameImage> lightIncidentImage; // incident diffuse (direct light, pre-albedo)
    std::unique_ptr<FrameImage> accumulationImage;
    std::unique_ptr<FrameImage> revealageImage;
    std::unique_ptr<FrameImage> giIndirectImage;
    std::unique_ptr<FrameImage> compositionColorImage;
    std::unique_ptr<FrameImage> smaaEdgeImage;
    std::unique_ptr<FrameImage> smaaBlendImage;
    std::unique_ptr<FrameImage> postAAColorImage;                               // HDR post-AA chain
    std::array<std::unique_ptr<FrameImage>, RC_CASCADE_COUNT> rcRadianceImages; // RC atlas per cascade

    std::array<std::vector<VkDescriptorSet>, MAX_FRAMES_IN_FLIGHT> depthPyramidMipDescriptorSets{};

    VkSampler lightPassSampler{VK_NULL_HANDLE};
    // Depth pyramid is used for ray-depth comparisons; it must be sampled with POINT/NEAREST filtering.
    VkSampler depthPyramidSampler{VK_NULL_HANDLE};
    VkSampler postProcessSampler{VK_NULL_HANDLE};

    // Shadow maps: [LightIndex][FrameIndex] - each light has MAX_FRAMES_IN_FLIGHT shadow maps
    std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_DIRECTIONAL_LIGHTS> directionalMaps;
    std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_POINT_LIGHTS> pointlightMaps;
    std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_SPOT_LIGHTS> spotlightMaps;

    // SMAA LUTs (shared) - manually managed for proper sampler/mip settings
    VkImage smaaAreaImage{VK_NULL_HANDLE};
    VkDeviceMemory smaaAreaMemory{VK_NULL_HANDLE};
    VkImageView smaaAreaView{VK_NULL_HANDLE};
    VkSampler smaaAreaSampler{VK_NULL_HANDLE};

    VkImage smaaSearchImage{VK_NULL_HANDLE};
    VkDeviceMemory smaaSearchMemory{VK_NULL_HANDLE};
    VkImageView smaaSearchView{VK_NULL_HANDLE};
    VkSampler smaaSearchSampler{VK_NULL_HANDLE};

    Texture* skyboxTexture{nullptr};

    VkDescriptorSetLayout materialDescriptorSetLayout{VK_NULL_HANDLE}; // borrowed, see the constructor
    VkDescriptorSetLayout modelsDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout cameraDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout gBufferDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout lightArrayDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout cascadeSplitsSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout sceneLightingDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout shadowcastinglightMatrixDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout shadowModelMatrixDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout shadowMapSamplerLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout skyboxDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout transparencyModelDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout compositionSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout rcBuildSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout rcResolveSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout depthPyramidSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout smaaEdgeSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout smaaWeightSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout smaaBlendSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout colorCorrectionSetLayout{VK_NULL_HANDLE};

    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> modelsDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> cameraDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> gBufferDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> lightArrayDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> cascadeSplitsDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> sceneLightingDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> lightMatrixDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> shadowModelMatrixDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> shadowMapSamplerSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> transparencyModelMatrixDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> compositionDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> rcBuildDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> rcResolveDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> depthPyramidDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> smaaEdgeDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> smaaWeightDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> smaaBlendDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> colorCorrectionDescriptorSets{};
    VkDescriptorSet skyboxDescriptorSet{VK_NULL_HANDLE};

    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> modelMatrixBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> normalMatrixBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> cameraUniformBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> lightArrayUniformBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> cascadeSplitsBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> sceneLightingBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> lightMatrixBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> shadowModelMatrixBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> transparencyModelMatrixBuffers{};
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> transparencyNormalMatrixBuffers{};
};

} // namespace Rendering