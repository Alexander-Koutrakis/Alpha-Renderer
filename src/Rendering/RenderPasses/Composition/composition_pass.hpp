#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "Rendering/Core/frame_context.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/Core/descriptors.hpp"
#include "Rendering/rendering_constants.hpp"
#include "Rendering/RenderPasses/Direct Lighting/light_pass.hpp"
#include "Rendering/RenderPasses/Transparency/transparency_pass.hpp"
#include "Rendering/Resources/rendering_resources.hpp"
#include <array>

namespace Rendering {

class CompositionPass {
public:
    struct CreateInfo {
        uint32_t width;
        uint32_t height;
        VkDescriptorSetLayout compositionDescriptorSetLayout;
        VkFormat targetFormat;
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>* targetViews;
    };

    CompositionPass(
        Device& device,
        const CreateInfo& createInfo
    );
    ~CompositionPass();

    CompositionPass(const CompositionPass&) = delete;
    CompositionPass& operator=(const CompositionPass&) = delete;

    void run(FrameContext& frameContext);

private:
    void cleanup();
    void createRenderPass();
    void createPipeline(const CreateInfo& createInfo);
    void createFramebuffers();

    void beginRenderPass(FrameContext& frameContext);
    void endRenderPass(FrameContext& frameContext);

    Device& device;
    uint32_t width;
    uint32_t height;
    VkFormat targetFormat;
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>* targetViews;

    VkRenderPass renderPass{VK_NULL_HANDLE};
    std::unique_ptr<Pipeline> pipeline{nullptr};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};

    // Framebuffers targeting offscreen or swapchain-provided views
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers{};
};

} // namespace Rendering 