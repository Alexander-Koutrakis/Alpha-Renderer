#include "light_pass.hpp"
#include "ECS/ecs.hpp"
#include "Rendering/Core/barriers.hpp"
#include "Rendering/Core/pipeline_layouts.hpp"
#include "Rendering/Core/render_passes.hpp"
#include <array>
#include <stdexcept>
#include <iostream>
#include <vector>

using namespace ECS;
using namespace Systems;
namespace Rendering {

LightPass::LightPass(Device& device, SwapChain& swapChain, const CreateInfo& createInfo)
    : device{device}, swapChain{swapChain}, width{createInfo.width}, height{createInfo.height} {
    createRenderPass(createInfo);
    createFramebuffers(createInfo);
    createPipeline(createInfo);
}

LightPass::~LightPass() {
    cleanup();
}

void LightPass::cleanup() {
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

    // Clean up descriptor pool and buffers
    std::cout << "Light pass cleaned up" << std::endl;
}

void LightPass::run(FrameContext& frameContext) {
    setBarriers(frameContext);

    beginRenderPass(frameContext);
    vkCmdBindPipeline(frameContext.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

    std::array<VkDescriptorSet, 7> descriptorSets = {
        frameContext.sceneLightingDescriptorSet, frameContext.lightArrayDescriptorSet,
        frameContext.gBufferDescriptorSet,       frameContext.shadowMapSamplerDescriptorSet,
        frameContext.lightMatrixDescriptorSet,   frameContext.skyboxDescriptorSet,
        frameContext.cascadeSplitsDescriptorSet};

    vkCmdBindDescriptorSets(frameContext.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                            static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, nullptr);

    vkCmdDraw(frameContext.commandBuffer, 3, 1, 0, 0);

    endRenderPass(frameContext);
}

void LightPass::createRenderPass(const CreateInfo& createInfo) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = createInfo.lightPassFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription incidentAttachment = colorAttachment;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference incidentAttachmentRef{};
    incidentAttachmentRef.attachment = 1;
    incidentAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkAttachmentReference colorRefs[2] = {colorAttachmentRef, incidentAttachmentRef};
    subpass.colorAttachmentCount = 2;
    subpass.pColorAttachments = colorRefs;

    // Add subpass dependency to handle layout transition
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = 0;

    std::array<VkAttachmentDescription, 2> attachments{colorAttachment, incidentAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create light pass render pass!");
    }
}

void LightPass::createFramebuffers(const CreateInfo& createInfo) {
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> imageViews = *createInfo.lightPassResultViewsPtr;
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> incidentViews = *createInfo.lightIncidentViewsPtr;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        framebuffers[i] =
            createFramebuffer(device.getDevice(), renderPass, {imageViews[i], incidentViews[i]}, width, height);
    }
}

void LightPass::createPipeline(const CreateInfo& createInfo) {
    std::vector<VkDescriptorSetLayout> setLayouts = {createInfo.sceneLightingDescriptorSetLayout,
                                                     createInfo.lightArrayDescriptorSetLayout,
                                                     createInfo.gBufferDescriptorSetLayout,
                                                     createInfo.shadowSamplerSetLayout,
                                                     createInfo.shadowMatrixSetLayout,
                                                     createInfo.enviromentalReflectionsSetLayout,
                                                     createInfo.cascadeSplitsSetLayout};

    pipelineLayout = createPipelineLayout(device.getDevice(), setLayouts);
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

    // Two MRT attachments (shaded + incident); reuse same blend state
    std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{};
    blendAttachments[0] = pipelineConfig.colorBlendAttachment;
    blendAttachments[1] = pipelineConfig.colorBlendAttachment;
    pipelineConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    pipelineConfig.colorBlendInfo.pAttachments = blendAttachments.data();

    // Create the pipeline with shaders
    std::vector<ShaderStageInfo> stages = {{VK_SHADER_STAGE_VERTEX_BIT, "shaders/direct_light.vert.spv"},
                                           {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/direct_light.frag.spv"}};
    pipeline = std::make_unique<Pipeline>(device, stages, pipelineConfig);
}

void LightPass::beginRenderPass(FrameContext& frameContext) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;

    if (framebuffers[frameContext.frameIndex] == VK_NULL_HANDLE) {
        throw std::runtime_error("Framebuffer is null in beginRenderPass!");
    }
    renderPassInfo.framebuffer = framebuffers[frameContext.frameIndex];

    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};

    VkClearValue clearValues[2]{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(frameContext.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void LightPass::endRenderPass(FrameContext& frameContext) {
    vkCmdEndRenderPass(frameContext.commandBuffer);
}

void LightPass::setBarriers(FrameContext& frameContext) {
    // UBOs written by the host this frame must be visible to the lighting fragment shader.
    const std::array<BufferBarrierDesc, 3> bufferBarriers{{
        BufferBarrierDesc::hostWriteToShaderRead(frameContext.sceneLightingBuffer->getBuffer()),
        BufferBarrierDesc::hostWriteToShaderRead(frameContext.lightArrayUniformBuffer->getBuffer()),
        BufferBarrierDesc::hostWriteToShaderRead(frameContext.cascadeSplitsBuffer->getBuffer()),
    }};

    // G-Buffer images are written by the geometry pass; they already sit in their read-only layout.
    constexpr VkImageLayout gBufferLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    constexpr VkImageLayout depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    const std::array<ImageBarrierDesc, 5> imageBarriers{{
        {frameContext.gBufferPositionImage, VK_IMAGE_ASPECT_COLOR_BIT, gBufferLayout, gBufferLayout,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT},
        {frameContext.gBufferNormalImage, VK_IMAGE_ASPECT_COLOR_BIT, gBufferLayout, gBufferLayout,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT},
        {frameContext.gBufferAlbedoImage, VK_IMAGE_ASPECT_COLOR_BIT, gBufferLayout, gBufferLayout,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT},
        {frameContext.gbufferMaterialImage, VK_IMAGE_ASPECT_COLOR_BIT, gBufferLayout, gBufferLayout,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT},
        {frameContext.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, depthLayout, depthLayout,
         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT},
    }};

    cmdPipelineBarrier(frameContext.commandBuffer,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, {}, bufferBarriers, imageBarriers);
}

} // namespace Rendering