#pragma once

// lib
#include "core.hpp"
#include "Rendering/Core/device.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/Core/buffer.hpp"
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
        RenderingResources(Device& device, SwapChain& swapChain);
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
        VkDescriptorSetLayout getShadowcastingLightMatrixDescriptorSetLayout() const { return shadowcastinglightMatrixDescriptorSetLayout; }
        VkDescriptorSetLayout getShadowModelMatrixDescriptorSetLayout() const { return shadowModelMatrixDescriptorSetLayout; }
        VkDescriptorSetLayout getEnvironmentalReflectionsDescriptorSetLayout() const { return skyboxDescriptorSetLayout; }
        VkDescriptorSetLayout getSkyboxDescriptorSetLayout() const { return skyboxDescriptorSetLayout; }
        VkDescriptorSetLayout getTransparencyModelDescriptorSetLayout() const { return transparencyModelDescriptorSetLayout; }
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
        ShadowMap& getDirectionalShadowMap(size_t lightIndex, size_t frameIndex) { return *directionalMaps[lightIndex][frameIndex]; }
        ShadowMap& getSpotShadowMap(size_t lightIndex, size_t frameIndex) { return *spotlightMaps[lightIndex][frameIndex]; }
        ShadowMap& getPointShadowMap(size_t lightIndex, size_t frameIndex) { return *pointlightMaps[lightIndex][frameIndex]; }
        
        // Shadow map sampler descriptor set accessor
        VkDescriptorSet getShadowMapSamplerDescriptorSet(size_t frameIndex) { return shadowMapSamplerSets[frameIndex]; }

        // GBuffer accessor
        GBuffer& getGBuffer()  { return *gBuffer; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getDepthViews() { return depthViews; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getLightPassResultViews() { return lightPassResultViews; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getLightIncidentViews() { return lightIncidentViews; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getAlbedoViews() { return gBuffer->getAlbedoViews(); }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getAccumulationViews() { return accumulationViews; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getRevealageViews() { return revealageViews; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getCompositionColorViews() { return compositionColorViews; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getSMAAEdgeViews() { return smaaEdgeViews; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getSMAABlendViews() { return smaaBlendViews; }
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getPostAAColorViews() { return postAAColorViews; }

        auto& getDirectionalLightMaps() {return directionalMaps;}
        auto& getPointLightMaps() {return pointlightMaps;}
        auto& getSpotLightMaps() {return spotlightMaps;}

        // Skybox update method
        void updateSkyboxDescriptorSet(VkImageView skyboxImageView, VkSampler skyboxSampler);
        void initializeSkyboxFromScene();

        std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> createFrameContexts();
    private:
        // Debug naming helper
        void setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name);

        void findResourcesFormats();
        void createDepthResources();
        void createDepthPyramidResources();
        void createLightPassResources();
        void createShadowMapResources();
        void createDescriptorPool();
        void createBuffers();
        void createDescriptorSetLayouts();
        void createDescriptorSets();
        void createShadowMapSamplerDescriptorSets();
        void createTransparencyResources();
        void createGIResources();
        void createRCAtlases();
        void createPostProcessResources();
        void loadSMAALUTTextures();



        std::array<FrameContext,MAX_FRAMES_IN_FLIGHT> frameContexts;


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
        // Incident diffuse buffer (direct light, pre-albedo)
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> lightIncidentImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> lightIncidentMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> lightIncidentViews{};

        std::unique_ptr<DescriptorPool> descriptorPool{nullptr};

        std::array<VkImage,MAX_FRAMES_IN_FLIGHT> depthImages{};
        std::array<VkDeviceMemory,MAX_FRAMES_IN_FLIGHT> depthMemories{};
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT> depthViews{};

        // Depth pyramid (linearized depth, mip chain)
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> depthPyramidImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> depthPyramidMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> depthPyramidViews{};
        std::array<uint32_t, MAX_FRAMES_IN_FLIGHT> depthPyramidMipLevels{};
        std::array<std::vector<VkImageView>, MAX_FRAMES_IN_FLIGHT> depthPyramidMipStorageViews{};
        std::array<std::vector<VkDescriptorSet>, MAX_FRAMES_IN_FLIGHT> depthPyramidMipDescriptorSets{};

        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> lightPassResultImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> lightPassResultMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> lightPassResultViews{};
        VkSampler lightPassSampler{VK_NULL_HANDLE};
        // Depth pyramid is used for ray-depth comparisons; it must be sampled with POINT/NEAREST filtering.
        VkSampler depthPyramidSampler{VK_NULL_HANDLE};
        VkSampler postProcessSampler{VK_NULL_HANDLE};

        // Shadow maps: [LightIndex][FrameIndex] - each light has MAX_FRAMES_IN_FLIGHT shadow maps
        std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_DIRECTIONAL_LIGHTS> directionalMaps;
        std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_POINT_LIGHTS> pointlightMaps;
        std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_SPOT_LIGHTS> spotlightMaps;

        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> accumulationImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> accumulationMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> accumulationViews{};
    
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> revealageImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> revealageMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> revealageViews{};

        // Indirect GI buffer (per-frame)
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> giIndirectImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> giIndirectMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> giIndirectViews{};

        // Post-process render targets
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> compositionColorImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> compositionColorMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> compositionColorViews{};

        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> smaaEdgeImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> smaaEdgeMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> smaaEdgeViews{};

        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> smaaBlendImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> smaaBlendMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> smaaBlendViews{};

        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> postAAColorImages{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> postAAColorMemories{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> postAAColorViews{};

        // SMAA LUTs (shared) - manually managed for proper sampler/mip settings
        VkImage smaaAreaImage{VK_NULL_HANDLE};
        VkDeviceMemory smaaAreaMemory{VK_NULL_HANDLE};
        VkImageView smaaAreaView{VK_NULL_HANDLE};
        VkSampler smaaAreaSampler{VK_NULL_HANDLE};
        
        VkImage smaaSearchImage{VK_NULL_HANDLE};
        VkDeviceMemory smaaSearchMemory{VK_NULL_HANDLE};
        VkImageView smaaSearchView{VK_NULL_HANDLE};
        VkSampler smaaSearchSampler{VK_NULL_HANDLE};

        // RC atlases per cascade (per frame)
        std::array<std::array<VkImage, MAX_FRAMES_IN_FLIGHT>, RC_CASCADE_COUNT> rcRadianceImages{};
        std::array<std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT>, RC_CASCADE_COUNT> rcRadianceMemories{};
        std::array<std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>, RC_CASCADE_COUNT> rcRadianceViews{};

        Texture* skyboxTexture{nullptr};

        VkDescriptorSetLayout materialDescriptorSetLayout{VK_NULL_HANDLE};
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
        std::array<std::unique_ptr<Buffer>,MAX_FRAMES_IN_FLIGHT> cameraUniformBuffers{};
        std::array<std::unique_ptr<Buffer>,MAX_FRAMES_IN_FLIGHT> lightArrayUniformBuffers{};
        std::array<std::unique_ptr<Buffer>,MAX_FRAMES_IN_FLIGHT> cascadeSplitsBuffers{};
        std::array<std::unique_ptr<Buffer>,MAX_FRAMES_IN_FLIGHT> sceneLightingBuffers{};
        std::array<std::unique_ptr<Buffer>,MAX_FRAMES_IN_FLIGHT> lightMatrixBuffers{};
        std::array<std::unique_ptr<Buffer>,MAX_FRAMES_IN_FLIGHT> shadowModelMatrixBuffers{};
        std::array<std::unique_ptr<Buffer>,MAX_FRAMES_IN_FLIGHT> transparencyModelMatrixBuffers{};
        std::array<std::unique_ptr<Buffer>,MAX_FRAMES_IN_FLIGHT> transparencyNormalMatrixBuffers{};
    };

} // namespace Rendering