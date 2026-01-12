#pragma once
#include "Rendering/Core/device.hpp"
#include "Rendering/Resources/gbuffer.hpp"
#include "Rendering/Core/frame_context.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "ECS/ecs.hpp"
#include "ECS/components.hpp"
#include "ECS/ecs_types.hpp"
#include "Rendering/rendering_constants.hpp"
#include <array>

using namespace ECS;
namespace Rendering {

class TransparencyPass {

public:
    struct CreateInfo {
        uint32_t width;
        uint32_t height;
        VkDescriptorSetLayout cameraDescriptorSetLayout;
        VkDescriptorSetLayout lightArrayDescriptorSetLayout;
        VkDescriptorSetLayout shadowMapSamplerLayout;
        VkDescriptorSetLayout transparencyModelDescriptorSetLayout;
        VkDescriptorSetLayout materialDescriptorSetLayout;
        VkDescriptorSetLayout sceneLightingDescriptorSetLayout;    
        VkDescriptorSetLayout lightMatrixDescriptorSetLayout;
        VkDescriptorSetLayout cascadeSplitsDescriptorSetLayout;
        VkFormat hdrFormat;
        VkFormat revealageFormat;
        VkFormat depthFormat;
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>* accumulationViewsPtr;
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>* revealageViewsPtr;
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>* depthViewsPtr;
    };

    TransparencyPass(
        Device& device, 
        const CreateInfo& createInfo
        );
    ~TransparencyPass();

    VkRenderPass getRenderPass() const { return renderPass; }
    void run(FrameContext& frameContext);
    
    
private:
    void cleanup();
    void createRenderPass(const CreateInfo& createInfo);
    void createPipeline(const CreateInfo& createInfo);
    void createFramebuffers(const CreateInfo& createInfo);

    void setBarriers(FrameContext& frameContext);
    void beginRenderPass(FrameContext& frameContext);
    void endRenderPass(FrameContext& frameContext);
    void updateDescriptorSets(FrameContext& frameContext);
    void drawBatches(FrameContext& frameContext);

    Device& device;
    uint32_t width;
    uint32_t height;
    

    VkRenderPass renderPass{VK_NULL_HANDLE};
    std::unique_ptr<Pipeline> pipeline{nullptr};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers{};
};

} 