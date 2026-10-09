#include "composition_pass.hpp"
#include "Rendering/Core/pipeline_layouts.hpp"
#include "Rendering/Core/render_passes.hpp"
#include <stdexcept>
#include <array>
#include <vector>

namespace Rendering {

CompositionPass::CompositionPass(Device& device, const CreateInfo& createInfo)
    : device{device},
      width{createInfo.width},
      height{createInfo.height},
      targetFormat{createInfo.targetFormat},
      targetViews{createInfo.targetViews} {
    createRenderPass();
    createFramebuffers();
    createPipeline(createInfo);
}

CompositionPass::~CompositionPass() {
    cleanup();
}

void CompositionPass::cleanup() {
    // Clean up framebuffers
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device.getDevice(), framebuffer, nullptr);
    }

    // Clean up pipeline resources
    pipeline.reset();
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    // Clean up render pass
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.getDevice(), renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

void CompositionPass::createRenderPass() {
    VkAttachmentDescription colorAttachment =
        attachmentDescription(targetFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    renderPass = Rendering::createRenderPass(device.getDevice(), colorAttachment, subpass, dependency);
}

void CompositionPass::createFramebuffers() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 1> attachments = {(*targetViews)[i]};

        framebuffers[i] = createFramebuffer(device.getDevice(), renderPass, attachments, width, height);
    }
}

void CompositionPass::createPipeline(const CreateInfo& createInfo) {
    pipelineLayout = createPipelineLayout(device.getDevice(), createInfo.compositionDescriptorSetLayout);

    // Create pipeline configuration
    PipelineConfigInfo pipelineConfig{};
    Pipeline::defaultPipelineConfigInfo(pipelineConfig);

    // Modify for fullscreen quad rendering
    pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    pipelineConfig.bindingDescriptions.clear();
    pipelineConfig.attributeDescriptions.clear();
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout;

    // Create the pipeline with shaders
    std::vector<ShaderStageInfo> stages = {{VK_SHADER_STAGE_VERTEX_BIT, "shaders/fullscreen.vert.spv"},
                                           {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/composition.frag.spv"}};
    pipeline = std::make_unique<Pipeline>(device, stages, pipelineConfig);
}

void CompositionPass::beginRenderPass(FrameContext& frameContext) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[frameContext.frameIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(frameContext.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CompositionPass::endRenderPass(FrameContext& frameContext) {
    vkCmdEndRenderPass(frameContext.commandBuffer);
}

void CompositionPass::run(FrameContext& frameContext) {
    beginRenderPass(frameContext);

    // Bind the composition pipeline
    vkCmdBindPipeline(frameContext.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

    // Bind descriptor set with the composition textures
    VkDescriptorSet compositionDescriptorSet = frameContext.compositionDescriptorSet;
    vkCmdBindDescriptorSets(frameContext.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                            &compositionDescriptorSet, 0, nullptr);

    // Draw fullscreen quad (3 vertices for screen-aligned triangle)
    vkCmdDraw(frameContext.commandBuffer, 3, 1, 0, 0);

    endRenderPass(frameContext);
}

} // namespace Rendering