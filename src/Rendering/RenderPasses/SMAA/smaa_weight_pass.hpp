#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "Rendering/Core/frame_context.hpp"

namespace Rendering {

class SMAAWeightPass {
public:
    struct CreateInfo {
        uint32_t width;
        uint32_t height;
        VkFormat targetFormat;
        VkDescriptorSetLayout descriptorSetLayout;
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>* targetViews;
    };

    SMAAWeightPass(Device& device, const CreateInfo& info);
    ~SMAAWeightPass();

    SMAAWeightPass(const SMAAWeightPass&) = delete;
    SMAAWeightPass& operator=(const SMAAWeightPass&) = delete;

    void run(FrameContext& frameContext);

private:
    void cleanup();
    void createRenderPass();
    void createFramebuffers();
    void createPipeline();
    void beginRenderPass(FrameContext& frameContext);
    void endRenderPass(FrameContext& frameContext);

    Device& device;
    uint32_t width;
    uint32_t height;
    VkFormat targetFormat;
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>* targetViews;
    VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};

    VkRenderPass renderPass{VK_NULL_HANDLE};
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers{};
    std::unique_ptr<Pipeline> pipeline{nullptr};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
};

} // namespace Rendering

