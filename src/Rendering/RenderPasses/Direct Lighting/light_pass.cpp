#include "light_pass.hpp"
#include "ECS/ecs.hpp"
#include <array>
#include <stdexcept>
#include <iostream>
#include <vector>

using namespace ECS;
using namespace Systems;
namespace Rendering {

LightPass::LightPass(
    Device& device, 
    SwapChain& swapChain, 
    const CreateInfo& createInfo)
    : device{device},
      swapChain{swapChain},
      width{createInfo.width},
      height{createInfo.height} {
    

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
    vkCmdBindPipeline(
        frameContext.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline->getPipeline()
    );

    std::array<VkDescriptorSet, 7> descriptorSets = {
        frameContext.sceneLightingDescriptorSet,
        frameContext.lightArrayDescriptorSet,
        frameContext.gBufferDescriptorSet,
        frameContext.shadowMapSamplerDescriptorSet,
        frameContext.lightMatrixDescriptorSet,
        frameContext.skyboxDescriptorSet,
        frameContext.cascadeSplitsDescriptorSet
    };

    vkCmdBindDescriptorSets(
        frameContext.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        static_cast<uint32_t>(descriptorSets.size()),
        descriptorSets.data(),
        0,
        nullptr
    );

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
    VkAttachmentReference colorRefs[2] = { colorAttachmentRef, incidentAttachmentRef };
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



VkWriteDescriptorSet LightPass::createWrite(VkDescriptorSet dstSet, uint32_t binding, VkDescriptorImageInfo* imageInfo) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dstSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = imageInfo;
    return write;
}


void LightPass::createFramebuffers(const CreateInfo& createInfo) {

    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> imageViews = *createInfo.lightPassResultViewsPtr;
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> incidentViews = *createInfo.lightIncidentViewsPtr;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {

        VkImageView attachments[2] = { imageViews[i], incidentViews[i] };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device.getDevice(), &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}


void LightPass::createPipeline(const CreateInfo& createInfo) {
   
    std::vector<VkDescriptorSetLayout> setLayouts = {
        createInfo.sceneLightingDescriptorSetLayout,
        createInfo.lightArrayDescriptorSetLayout,
        createInfo.gBufferDescriptorSetLayout,
        createInfo.shadowSamplerSetLayout,
        createInfo.shadowMatrixSetLayout,
        createInfo.enviromentalReflectionsSetLayout,
        createInfo.cascadeSplitsSetLayout
    };


    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
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

    // Two MRT attachments (shaded + incident); reuse same blend state
    std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{};
    blendAttachments[0] = pipelineConfig.colorBlendAttachment;
    blendAttachments[1] = pipelineConfig.colorBlendAttachment;
    pipelineConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    pipelineConfig.colorBlendInfo.pAttachments = blendAttachments.data();

    // Create the pipeline with shaders
    std::vector<ShaderStageInfo> stages = {
        {VK_SHADER_STAGE_VERTEX_BIT, "shaders/direct_light.vert.spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/direct_light.frag.spv"}
    };
    pipeline = std::make_unique<Pipeline>(
        device,
        stages,
        pipelineConfig
    );
    
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
    VkCommandBuffer commandBuffer = frameContext.commandBuffer;
    
    // Buffer barriers for UBOs
    std::array<VkBufferMemoryBarrier, 3> bufferBarriers{};  // Change from 1 to 3
    
    // Lighting UBO barrier
    bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarriers[0].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    bufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bufferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[0].buffer = frameContext.sceneLightingBuffer->getBuffer();
    bufferBarriers[0].offset = 0;
    bufferBarriers[0].size = VK_WHOLE_SIZE;

    // Unified light buffer barrier
    bufferBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarriers[1].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    bufferBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bufferBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[1].buffer = frameContext.lightArrayUniformBuffer->getBuffer();
    bufferBarriers[1].offset = 0;
    bufferBarriers[1].size = VK_WHOLE_SIZE;

    // Cascade splits buffer barrier
    bufferBarriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarriers[2].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    bufferBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bufferBarriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[2].buffer = frameContext.cascadeSplitsBuffer->getBuffer();
    bufferBarriers[2].offset = 0;
    bufferBarriers[2].size = VK_WHOLE_SIZE;
    
    // Need to wait for G-Buffer images to be written by Geometry Pass
    std::array<VkImageMemoryBarrier, 5> imageBarriers{};
    
    // Position buffer barrier
    imageBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // Should already be in this layout
    imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[0].image = frameContext.gBufferPositionImage;
    imageBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarriers[0].subresourceRange.baseMipLevel = 0;
    imageBarriers[0].subresourceRange.levelCount = 1;
    imageBarriers[0].subresourceRange.baseArrayLayer = 0;
    imageBarriers[0].subresourceRange.layerCount = 1;
    
    // Normal buffer barrier
    imageBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[1].image = frameContext.gBufferNormalImage;
    imageBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarriers[1].subresourceRange.baseMipLevel = 0;
    imageBarriers[1].subresourceRange.levelCount = 1;
    imageBarriers[1].subresourceRange.baseArrayLayer = 0;
    imageBarriers[1].subresourceRange.layerCount = 1;
    
    // Albedo buffer barrier
    imageBarriers[2].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarriers[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageBarriers[2].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarriers[2].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[2].image = frameContext.gBufferAlbedoImage;
    imageBarriers[2].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarriers[2].subresourceRange.baseMipLevel = 0;
    imageBarriers[2].subresourceRange.levelCount = 1;
    imageBarriers[2].subresourceRange.baseArrayLayer = 0;
    imageBarriers[2].subresourceRange.layerCount = 1;
    
    // Material buffer barrier
    imageBarriers[3].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarriers[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarriers[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageBarriers[3].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarriers[3].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarriers[3].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[3].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[3].image = frameContext.gbufferMaterialImage;
    imageBarriers[3].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarriers[3].subresourceRange.baseMipLevel = 0;
    imageBarriers[3].subresourceRange.levelCount = 1;
    imageBarriers[3].subresourceRange.baseArrayLayer = 0;
    imageBarriers[3].subresourceRange.layerCount = 1;
 
    // Depth buffer barrier
    imageBarriers[4].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarriers[4].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    imageBarriers[4].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageBarriers[4].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageBarriers[4].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageBarriers[4].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[4].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarriers[4].image = frameContext.depthImage;
    imageBarriers[4].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    imageBarriers[4].subresourceRange.baseMipLevel = 0;
    imageBarriers[4].subresourceRange.levelCount = 1;
    imageBarriers[4].subresourceRange.baseArrayLayer = 0;
    imageBarriers[4].subresourceRange.layerCount = 1;

    

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(),
        static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data()
    );
}

} // namespace Rendering