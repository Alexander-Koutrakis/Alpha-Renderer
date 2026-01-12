#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "Rendering/Core/descriptors.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/Resources/gbuffer.hpp"
#include "Rendering/rendering_constants.hpp"
#include "Scene/scene.hpp"
#include "Systems/transform_system.hpp"

#include "Rendering/Core/frame_context.hpp"
namespace Rendering {

class LightPass {
public:

    struct CreateInfo {
        uint32_t width;
        uint32_t height;
        VkDescriptorSetLayout cameraDescriptorSetLayout;
        VkDescriptorSetLayout modelsDescriptorSetLayout;
        VkDescriptorSetLayout materialDescriptorSetLayout;
        VkDescriptorSetLayout gBufferDescriptorSetLayout;
        VkDescriptorSetLayout lightArrayDescriptorSetLayout;
        VkDescriptorSetLayout cascadeSplitsSetLayout;
        VkDescriptorSetLayout sceneLightingDescriptorSetLayout;
        VkDescriptorSetLayout shadowSamplerSetLayout;
        VkDescriptorSetLayout shadowMatrixSetLayout;
        VkDescriptorSetLayout enviromentalReflectionsSetLayout;
        VkFormat lightPassFormat;
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>* lightPassResultViewsPtr;
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>* lightIncidentViewsPtr;
    };

    LightPass(
        Device& device, 
        SwapChain& swapChain, 
        const CreateInfo& createInfo);
    ~LightPass();

    LightPass(const LightPass&) = delete;
    LightPass& operator=(const LightPass&) = delete;

    void run(FrameContext& frameContext);
   
   
private:
    void cleanup();
    void createRenderPass(const CreateInfo& createInfo);
    void createPipeline(const CreateInfo& createInfo);
    void createFramebuffers(const CreateInfo& createInfo);

    void transitionGBufferImages(VkCommandBuffer commandBuffer);
    void setBarriers(FrameContext& frameContext);
    VkWriteDescriptorSet createWrite(VkDescriptorSet dstSet, uint32_t binding, VkDescriptorImageInfo* imageInfo);
    void beginRenderPass(FrameContext& frameContext);
    void endRenderPass(FrameContext& frameContext);

    Device& device;
    SwapChain& swapChain;    
    uint32_t width;
    uint32_t height;

    VkRenderPass renderPass{VK_NULL_HANDLE};
    std::unique_ptr<Pipeline> pipeline{nullptr};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};


    // Framebuffers for rendering
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers{};
};

} // namespace Rendering