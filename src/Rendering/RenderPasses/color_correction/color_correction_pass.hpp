#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "Rendering/Core/frame_context.hpp"
#include "Rendering/Core/swapchain.hpp"

namespace Rendering {

class ColorCorrectionPass {
public:
    struct CreateInfo {
        uint32_t width;
        uint32_t height;
        VkFormat targetFormat;
        VkDescriptorSetLayout descriptorSetLayout;
        const std::vector<VkImageView>* targetViews; // one per swapchain image
    };

    ColorCorrectionPass(Device& device, const CreateInfo& info);
    ~ColorCorrectionPass();

    ColorCorrectionPass(const ColorCorrectionPass&) = delete;
    ColorCorrectionPass& operator=(const ColorCorrectionPass&) = delete;

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
    const std::vector<VkImageView>* targetViews;
    VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};

    VkRenderPass renderPass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers; // indexed by swapchain image
    std::unique_ptr<Pipeline> pipeline{nullptr};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
};

} // namespace Rendering
