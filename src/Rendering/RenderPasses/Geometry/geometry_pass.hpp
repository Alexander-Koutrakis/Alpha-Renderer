#pragma once
#include "Rendering/Core/device.hpp"
#include "Rendering/Resources/gbuffer.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "ECS/ecs.hpp"
#include "ECS/components.hpp"
#include "ECS/ecs_types.hpp"
#include "Rendering/rendering_constants.hpp"
#include "Rendering/Resources/rendering_resources.hpp"
#include "Rendering/Core/frame_context.hpp"
#include <array>

using namespace ECS;
namespace Rendering {

class GeometryPass {

public:
    struct CreateInfo {
        uint32_t width;
        uint32_t height;
        VkDescriptorSetLayout cameraDescriptorSetLayout;
        VkDescriptorSetLayout modelsDescriptorSetLayout;
        VkDescriptorSetLayout materialDescriptorSetLayout;
        VkFormat depthFormat;
        VkFormat positionFormat;
        VkFormat normalFormat;
        VkFormat albedoFormat;
        VkFormat materialFormat;
        GBuffer* gBuffer;
        std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>* depthViewsPtr;
    };

    GeometryPass(Device& device, const CreateInfo& createInfo);
    ~GeometryPass();

   
    VkRenderPass getRenderPass() const { return renderPass; }
    void run(FrameContext& frameContext);
private:
    void cleanup();

    void createPipeline(const CreateInfo& createInfo);
    void createRenderPass(const CreateInfo& createInfo);
    void createFramebuffers(const CreateInfo& createInfo);
    void updateCameraModelMatrixDescriptors(FrameContext& frameContext);
    void drawBatches(FrameContext& frameContext);
    void beginRenderPass(FrameContext& frameContext);
    void endRenderPass(FrameContext& frameContext);
    void setBarriers(FrameContext& frameContext);
 
    Device& device;
    uint32_t width;
    uint32_t height;

    VkRenderPass renderPass{VK_NULL_HANDLE};
   
    std::unique_ptr<Pipeline> pipeline{nullptr};
    
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    std::array<VkFramebuffer,MAX_FRAMES_IN_FLIGHT> framebuffers{};  


};

}