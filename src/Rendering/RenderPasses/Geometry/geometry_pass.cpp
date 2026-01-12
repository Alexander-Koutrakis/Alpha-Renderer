#include "geometry_pass.hpp"
#include <array>
#include <stdexcept>
#include <iostream>
#include <vector>

namespace Rendering {

    GeometryPass::GeometryPass(Device& device,const  CreateInfo& createInfo)
        : device{device}, 
        width{createInfo.width}, 
        height{createInfo.height}
        {
         
        createRenderPass(createInfo);
        createFramebuffers(createInfo);  
        createPipeline(createInfo);  
    }

    GeometryPass::~GeometryPass() {
        cleanup();
    }

   

    void GeometryPass::cleanup() {
            // Clean up framebuffers
        for (auto framebuffer : framebuffers) {
            vkDestroyFramebuffer(device.getDevice(), framebuffer, nullptr);
        }


        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device.getDevice(), pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        pipeline.reset();

         // Clean up render pass and pipeline resources
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device.getDevice(), renderPass, nullptr);
        }

        std::cout << "Geometry pass cleaned up" << std::endl;
    }

    void GeometryPass::createRenderPass(const CreateInfo& createInfo) {
        std::array<VkAttachmentDescription, 5> attachmentDescriptions{};
        
        // Position attachment
        attachmentDescriptions[0].format = createInfo.positionFormat;
        attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Normal attachment
        attachmentDescriptions[1].format = createInfo.normalFormat;
        attachmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Albedo attachment
        attachmentDescriptions[2].format = createInfo.albedoFormat;
        attachmentDescriptions[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Material attachment
        attachmentDescriptions[3].format = createInfo.materialFormat;
        attachmentDescriptions[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Depth attachment
        attachmentDescriptions[4].format = createInfo.depthFormat;
        attachmentDescriptions[4].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        // Attachment references
        std::array<VkAttachmentReference, 4> colorRefs{};
        for (uint32_t i = 0; i < 4; i++) {
            colorRefs[i].attachment = i;
            colorRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkAttachmentReference depthRef{};
        depthRef.attachment = 4;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
        renderPassInfo.pAttachments = attachmentDescriptions.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(device.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render pass!");
        }
    }

    void GeometryPass::createFramebuffers(const CreateInfo& createInfo) {
        
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> depthViews = *createInfo.depthViewsPtr;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            std::array<VkImageView, 5> attachments = {
                createInfo.gBuffer->getPositionView(i),
                createInfo.gBuffer->getNormalView(i),
                createInfo.gBuffer->getAlbedoView(i),
                createInfo.gBuffer->getMaterialView(i),
                depthViews[i]
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

    void GeometryPass::beginRenderPass(FrameContext& frameContext) {
        std::array<VkClearValue, 5> clearValues{};
        clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};  // Position w=0 is used for cascade building
        clearValues[1].color = {0.0f, 0.0f, 0.0f, 1.0f};  // Normal
        clearValues[2].color = {0.0f, 0.0f, 0.0f, 1.0f};  // Albedo
        clearValues[3].color = {0.0f, 0.0f, 0.0f, 1.0f};  // Material
        clearValues[4].depthStencil = {1.0f, 0};          // Depth

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[frameContext.frameIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {width, height};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(frameContext.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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

    void GeometryPass::endRenderPass(FrameContext& frameContext) {
        vkCmdEndRenderPass(frameContext.commandBuffer);
    }
    
    void GeometryPass::createPipeline(const CreateInfo& createInfo){
        std::array<VkDescriptorSetLayout, 3> setLayouts = {
            createInfo.cameraDescriptorSetLayout,
            createInfo.modelsDescriptorSetLayout,
            createInfo.materialDescriptorSetLayout
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(uint32_t);

        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

        if (vkCreatePipelineLayout(device.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create pipeline layout!");
        }

        // Create pipeline configuration
        PipelineConfigInfo pipelineConfig{};
        Pipeline::defaultPipelineConfigInfo(pipelineConfig);
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;
        
        std::array<VkPipelineColorBlendAttachmentState, 4> colorBlendAttachments{};
        for(auto& attachment : colorBlendAttachments) {
            attachment = pipelineConfig.colorBlendAttachment;
        }

        pipelineConfig.colorBlendInfo.attachmentCount = 4;
        pipelineConfig.colorBlendInfo.pAttachments = colorBlendAttachments.data();
        pipelineConfig.rasterizationInfo.cullMode=VK_CULL_MODE_BACK_BIT;
        pipelineConfig.rasterizationInfo.frontFace=VK_FRONT_FACE_CLOCKWISE;
        pipelineConfig.depthStencilInfo.depthTestEnable = VK_TRUE;
        pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
        pipelineConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        // Create the pipeline
        
        std::vector<ShaderStageInfo> stages = {
            {VK_SHADER_STAGE_VERTEX_BIT, "shaders/geometry.vert.spv"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/geometry.frag.spv"}
        };
        pipeline = std::make_unique<Pipeline>(
            device,
            stages,
            pipelineConfig
        );

        

    }

    void GeometryPass::run(FrameContext& frameContext) {
        setBarriers(frameContext);
        beginRenderPass(frameContext);
        vkCmdBindPipeline(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline->getPipeline()
            );

        updateCameraModelMatrixDescriptors(frameContext);
        drawBatches(frameContext);
        endRenderPass(frameContext);
    }

    void GeometryPass::updateCameraModelMatrixDescriptors(FrameContext& frameContext) {
    
        std::array<VkDescriptorSet, 2> descriptorSets = {
            frameContext.cameraDescriptorSet, 
            frameContext.modelsDescriptorSet
        };

        vkCmdBindDescriptorSets(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,
            2,
            descriptorSets.data(),
            0,
            nullptr
        );
           
    }
  
    void GeometryPass::drawBatches(FrameContext& frameContext) {

        for (uint32_t i = 0; i < frameContext.opaqueMaterialBatchCount; i++) {
            const auto& materialBatch = frameContext.opaqueMaterialBatches[i];
            VkDescriptorSet materialDescriptorSet = materialBatch.material->getMaterialDescriptorSet();
            vkCmdBindDescriptorSets(
                frameContext.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                2,
                1,
                &materialDescriptorSet,
                0,
                nullptr
            );
        
            uint32_t bufferIndexOffset=materialBatch.matrixOffset;
            vkCmdPushConstants(
                frameContext.commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(uint32_t),
                &bufferIndexOffset
            );
       
            // Bind the mesh
            auto mesh = materialBatch.mesh;
            mesh->bind(frameContext.commandBuffer);           
            mesh->drawSubmeshInstanced(frameContext.commandBuffer, materialBatch.submeshIndex, materialBatch.instanceCount);
        }
        
    }
    
    void GeometryPass::setBarriers(FrameContext& frameContext) {
        VkCommandBuffer commandBuffer = frameContext.commandBuffer;
        // We need barriers for instance model matrices, normal matrices, and camera UBO
        std::array<VkBufferMemoryBarrier, 3> barriers{};
        
        // Instance model matrices barrier
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = frameContext.modelMatrixBuffer->getBuffer();
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;
        
        // Instance normal matrices barrier
        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[1].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].buffer = frameContext.normalMatrixBuffer->getBuffer();
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;
        
        // Camera uniform buffer barrier
        barriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[2].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].buffer = frameContext.cameraUniformBuffer->getBuffer();
        barriers[2].offset = 0;
        barriers[2].size = VK_WHOLE_SIZE;
        
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_HOST_BIT,               
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,      
            0,                                        
            0, nullptr,                                
            static_cast<uint32_t>(barriers.size()), barriers.data(),  
            0, nullptr                                 
        );
    }
}