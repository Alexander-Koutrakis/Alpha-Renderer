#include "color_correction_pass.hpp"
#include "Rendering/Core/pipeline_layouts.hpp"
#include "Rendering/Core/render_passes.hpp"
#include <array>
#include <stdexcept>

namespace Rendering {

ColorCorrectionPass::ColorCorrectionPass(Device& device, const CreateInfo& info)
    : device{device},
      width{info.width},
      height{info.height},
      targetFormat{info.targetFormat},
      targetViews{info.targetViews},
      descriptorSetLayout{info.descriptorSetLayout} {
    createRenderPass();
    createFramebuffers();
    createPipeline();
}

ColorCorrectionPass::~ColorCorrectionPass() {
    cleanup();
}

void ColorCorrectionPass::cleanup() {
    for (auto framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device.getDevice(), framebuffer, nullptr);
        }
    }
    framebuffers.fill(VK_NULL_HANDLE);

    pipeline.reset();
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.getDevice(), renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

void ColorCorrectionPass::createRenderPass() {
    VkAttachmentDescription colorAttachment =
        attachmentDescription(targetFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

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

void ColorCorrectionPass::createFramebuffers() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 1> attachments = {(*targetViews)[i]};

        framebuffers[i] = createFramebuffer(device.getDevice(), renderPass, attachments, width, height);
    }
}

void ColorCorrectionPass::createPipeline() {
    pipelineLayout = createPipelineLayout(device.getDevice(), descriptorSetLayout);

    PipelineConfigInfo pipelineConfig{};
    Pipeline::defaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout;
    pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    pipelineConfig.bindingDescriptions.clear();
    pipelineConfig.attributeDescriptions.clear();

    std::vector<ShaderStageInfo> stages = {{VK_SHADER_STAGE_VERTEX_BIT, "shaders/fullscreen.vert.spv"},
                                           {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/color_correction.frag.spv"}};

    pipeline = std::make_unique<Pipeline>(device, stages, pipelineConfig);
}

void ColorCorrectionPass::beginRenderPass(FrameContext& frameContext) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[frameContext.frameIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(frameContext.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void ColorCorrectionPass::endRenderPass(FrameContext& frameContext) {
    vkCmdEndRenderPass(frameContext.commandBuffer);
}

void ColorCorrectionPass::run(FrameContext& frameContext) {
    beginRenderPass(frameContext);

    vkCmdBindPipeline(frameContext.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

    VkDescriptorSet descriptorSet = frameContext.colorCorrectionDescriptorSet;
    vkCmdBindDescriptorSets(frameContext.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                            &descriptorSet, 0, nullptr);

    vkCmdDraw(frameContext.commandBuffer, 3, 1, 0, 0);

    endRenderPass(frameContext);
}

} // namespace Rendering
