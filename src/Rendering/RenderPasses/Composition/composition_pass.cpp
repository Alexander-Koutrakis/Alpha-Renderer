#include "composition_pass.hpp"
#include <stdexcept>
#include <array>
#include <vector>

namespace Rendering {

CompositionPass::CompositionPass(
    Device& device,
    const CreateInfo& createInfo
)
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

    std::cout << "Composition pass cleaned up" << std::endl;
}

void CompositionPass::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = targetFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create composition pass render pass!");
    }
}

void CompositionPass::createFramebuffers() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 1> attachments = {
            (*targetViews)[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device.getDevice(), &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}



void CompositionPass::createPipeline(const CreateInfo& createInfo) {
    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    VkDescriptorSetLayout compositionSetLayout = createInfo.compositionDescriptorSetLayout;

    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compositionSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

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
    std::vector<ShaderStageInfo> stages = {
        {VK_SHADER_STAGE_VERTEX_BIT, "shaders/fullscreen.vert.spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/composition.frag.spv"}
    };
    pipeline = std::make_unique<Pipeline>(
        device,
        stages,
        pipelineConfig
    );
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
    vkCmdBindPipeline(
        frameContext.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline->getPipeline()
    );

    // Bind descriptor set with the composition textures
    VkDescriptorSet compositionDescriptorSet = frameContext.compositionDescriptorSet;
    vkCmdBindDescriptorSets(
        frameContext.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        &compositionDescriptorSet,
        0,
        nullptr
    );

    // Draw fullscreen quad (3 vertices for screen-aligned triangle)
    vkCmdDraw(frameContext.commandBuffer, 3, 1, 0, 0);

    endRenderPass(frameContext);
}


} // namespace Rendering 