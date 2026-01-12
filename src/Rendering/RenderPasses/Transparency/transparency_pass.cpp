#include "transparency_pass.hpp"
#include <stdexcept>
#include <array>
#include <iostream>
#include <vector>

using namespace ECS;
namespace Rendering {

TransparencyPass::TransparencyPass(
    Device& device, 
    const CreateInfo& createInfo)
    :   device{device}, 
        width{createInfo.width}, 
        height{createInfo.height} {
    createRenderPass(createInfo);
    createFramebuffers(createInfo); 
    createPipeline(createInfo);   
}

TransparencyPass::~TransparencyPass() {
    cleanup();
}




void TransparencyPass::cleanup() {
    // Accumulation resources
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFramebuffer(device.getDevice(), framebuffers[i], nullptr);
    }
    
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    vkDestroyRenderPass(device.getDevice(), renderPass, nullptr);

    std::cout << "Transparency pass cleaned up" << std::endl;
}

void TransparencyPass::createRenderPass(const CreateInfo& createInfo) {
    // Color attachment for accumulation buffer
    VkAttachmentDescription accumulationAttachment{};
    accumulationAttachment.format = createInfo.hdrFormat;
    accumulationAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    accumulationAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    accumulationAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    accumulationAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    accumulationAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    accumulationAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    accumulationAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Color attachment for revealage buffer
    VkAttachmentDescription revealageAttachment{};
    revealageAttachment.format = createInfo.revealageFormat;
    revealageAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    revealageAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    revealageAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    revealageAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    revealageAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    revealageAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    revealageAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Depth attachment (read-only from geometry pass)
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = createInfo.depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    // References for attachment
    VkAttachmentReference accumulationRef{};
    accumulationRef.attachment = 0;
    accumulationRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference revealageRef{};
    revealageRef.attachment = 1;
    revealageRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference depthRef{};
    depthRef.attachment = 2;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    std::array<VkAttachmentReference, 2> colorRefs = {accumulationRef, revealageRef};
    
    // Subpass
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;
    
    // Dependencies
    std::array<VkSubpassDependency, 2> dependencies{};
    
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    // Create render pass
    std::array<VkAttachmentDescription, 3> attachments = {
        accumulationAttachment, revealageAttachment, depthAttachment
    };
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    
    if (vkCreateRenderPass(device.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transparency render pass");
    }
}




void TransparencyPass::createPipeline(const CreateInfo& createInfo) {
     // Create pipeline layout
     // Matches transparency.frag descriptor set layout:
     // Set 0: CameraUBO
     // Set 1: LightUbo (unified lights)
     // Set 2: Shadow map samplers
     // Set 3: Model matrices (vertex shader only)
     // Set 4: Material textures
     // Set 5: SceneLightingUbo
     // Set 6: ShadowcastingLightMatrices
     // Set 7: CascadeSplits
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {
        createInfo.cameraDescriptorSetLayout,              // Set 0
        createInfo.lightArrayDescriptorSetLayout,          // Set 1
        createInfo.shadowMapSamplerLayout,                 // Set 2
        createInfo.transparencyModelDescriptorSetLayout,   // Set 3
        createInfo.materialDescriptorSetLayout,            // Set 4
        createInfo.sceneLightingDescriptorSetLayout,       // Set 5
        createInfo.lightMatrixDescriptorSetLayout,         // Set 6
        createInfo.cascadeSplitsDescriptorSetLayout        // Set 7
    };
    

    VkPipelineLayoutCreateInfo instancedPipelineLayoutInfo{};
    instancedPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    instancedPipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    instancedPipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t);

    instancedPipelineLayoutInfo.pushConstantRangeCount = 1;
    instancedPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    
    if (vkCreatePipelineLayout(device.getDevice(), &instancedPipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transparency pipeline layout");
    }

    // Pipeline configuration - just the basics for now
    PipelineConfigInfo instancedPipelineConfig{};
    Pipeline::defaultPipelineConfigInfo(instancedPipelineConfig);
    
    // Create array of color blend attachment states
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(2);

    // Accumulation blend state (additive blending)
    colorBlendAttachments[0].blendEnable = VK_TRUE;
    colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    // Revealage blend state (multiplicative blending)
    colorBlendAttachments[1].blendEnable = VK_TRUE;
    colorBlendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    colorBlendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

    // Update color blend state to use our attachment states
    instancedPipelineConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    instancedPipelineConfig.colorBlendInfo.pAttachments = colorBlendAttachments.data();

    // Enable depth testing but disable depth writing for transparent objects
    instancedPipelineConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;
    instancedPipelineConfig.depthStencilInfo.depthTestEnable = VK_TRUE;
    
    //instancedPipelineConfig.bindingDescriptions.clear();
    //instancedPipelineConfig.attributeDescriptions.clear();

    instancedPipelineConfig.renderPass = renderPass;
    instancedPipelineConfig.pipelineLayout = pipelineLayout;
    std::vector<ShaderStageInfo> stages = {
        {VK_SHADER_STAGE_VERTEX_BIT, "shaders/transparency.vert.spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/transparency.frag.spv"}
    };
    pipeline = std::make_unique<Pipeline>(
            device,
            stages,
            instancedPipelineConfig
    );   
}

void TransparencyPass::createFramebuffers(const CreateInfo& createInfo) {

    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> accumulationViews = *createInfo.accumulationViewsPtr;
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> revealageViews = *createInfo.revealageViewsPtr;
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> depthViews = *createInfo.depthViewsPtr;

    // Create framebuffers for each frame in flight
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {

        std::array<VkImageView, 3> attachments = {
            accumulationViews[i],  // Accumulation buffer
            revealageViews[i],     // Revealage buffer
            depthViews[i] // Depth buffer
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
            throw std::runtime_error("Failed to create transparency framebuffer");
        }
    }
}


void TransparencyPass::beginRenderPass(FrameContext& frameContext) {
    // Begin the OIT render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[frameContext.frameIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};
    
    // Clear values for attachments
    std::array<VkClearValue, 3> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // Accumulation buffer - clear to zero for additive blending
    clearValues[1].color = {{1.0f, 0.0f, 0.0f, 0.0f}}; // Revealage buffer - start with 1.0 for multiplicative blending
    clearValues[2].depthStencil = {1.0f, 0};          // Depth (ignored due to LOAD op)

    
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    
    vkCmdBeginRenderPass(frameContext.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    
    vkCmdSetViewport(frameContext.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(frameContext.commandBuffer, 0, 1, &scissor);
}

void TransparencyPass::endRenderPass(FrameContext& frameContext) {
    vkCmdEndRenderPass(frameContext.commandBuffer);
}
void TransparencyPass::run(FrameContext& frameContext) {
    setBarriers(frameContext);      
    beginRenderPass(frameContext);
    vkCmdBindPipeline(
                frameContext.commandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, 
                pipeline->getPipeline()
            );          
    updateDescriptorSets(frameContext);  
    drawBatches(frameContext);
    endRenderPass(frameContext);
}

void TransparencyPass::drawBatches(FrameContext& frameContext) {

    uint32_t bufferIndexOffset = 0;
    
    // Draw each transparent material batch
    for (uint32_t i = 0; i < frameContext.transparentMaterialBatchCount; i++) {
        const auto& materialBatch = frameContext.transparentMaterialBatches[i];      
        VkDescriptorSet materialDescriptorSet = materialBatch.material->getMaterialDescriptorSet();
 
        vkCmdBindDescriptorSets(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            4,
            1,
            &materialDescriptorSet,
            0,
            nullptr
        );

        vkCmdPushConstants(
            frameContext.commandBuffer,
            pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(uint32_t),
            &bufferIndexOffset
        );

        auto mesh = materialBatch.mesh;
        uint32_t instanceCount = materialBatch.instanceCount;
            
        // Bind and draw the mesh with instances
        mesh->bind(frameContext.commandBuffer);
        mesh->drawSubmeshInstanced(frameContext.commandBuffer, materialBatch.submeshIndex, instanceCount);

        bufferIndexOffset += instanceCount;
    }
}


void TransparencyPass::updateDescriptorSets(FrameContext& frameContext) {
    
        // Set 0: CameraUBO
        // Set 1: LightUbo (unified lights)
        // Set 2: Shadow map samplers
        // Set 3: Model matrices
        // Set 4: Material (bound separately per-material in drawBatches)
        // Set 5: SceneLightingUbo
        // Set 6: ShadowcastingLightMatrices
        // Set 7: CascadeSplits
        std::array<VkDescriptorSet, 4> descriptorSets0_3 = {
            frameContext.cameraDescriptorSet,           // Set 0
            frameContext.lightArrayDescriptorSet,       // Set 1
            frameContext.shadowMapSamplerDescriptorSet, // Set 2
            frameContext.transparencyModelDescriptorSet // Set 3
        };

        vkCmdBindDescriptorSets(
                frameContext.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                0,
                static_cast<uint32_t>(descriptorSets0_3.size()),
                descriptorSets0_3.data(),
                0,
                nullptr
        );
        
        // Bind sets 5, 6, 7 (set 4 is material, bound per-draw in drawBatches)
        std::array<VkDescriptorSet, 3> descriptorSets5_7 = {
            frameContext.sceneLightingDescriptorSet,    // Set 5
            frameContext.lightMatrixDescriptorSet,      // Set 6
            frameContext.cascadeSplitsDescriptorSet     // Set 7
        };
        
        vkCmdBindDescriptorSets(
                frameContext.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                5,  // Starting at set 5
                static_cast<uint32_t>(descriptorSets5_7.size()),
                descriptorSets5_7.data(),
                0,
                nullptr
        );
     
}



void TransparencyPass::setBarriers(FrameContext& frameContext) {
    VkCommandBuffer commandBuffer = frameContext.commandBuffer;

    // Create barriers for all required buffers
    std::array<VkBufferMemoryBarrier, 5> barriers{};
    
    // Instance model matrices barrier
    barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].buffer = frameContext.transparencyModelMatrixBuffer->getBuffer();
    barriers[0].offset = 0;
    barriers[0].size = VK_WHOLE_SIZE;
    
    // Instance normal matrices barrier
    barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].buffer = frameContext.transparencyNormalMatrixBuffer->getBuffer();
    barriers[1].offset = 0;
    barriers[1].size = VK_WHOLE_SIZE;
    
    // Scene lighting UBO barrier
    barriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[2].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[2].buffer = frameContext.sceneLightingBuffer->getBuffer();
    barriers[2].offset = 0;
    barriers[2].size = VK_WHOLE_SIZE;
    
    // Light matrix buffer barrier
    barriers[3].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[3].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barriers[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[3].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[3].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[3].buffer = frameContext.lightMatrixBuffer->getBuffer();
    barriers[3].offset = 0;
    barriers[3].size = VK_WHOLE_SIZE;
    
    // Cascade splits buffer barrier
    barriers[4].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[4].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barriers[4].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[4].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[4].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[4].buffer = frameContext.cascadeSplitsBuffer->getBuffer();
    barriers[4].offset = 0;
    barriers[4].size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT,               // Source: CPU writes
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,                                         // No dependency flags needed
        0, nullptr,                                // No memory barriers
        static_cast<uint32_t>(barriers.size()), barriers.data(), // Buffer barriers
        0, nullptr                                 // No image barriers
    );
}



   

} 