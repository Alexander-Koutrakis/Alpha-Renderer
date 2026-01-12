#include "shadow_pass.hpp"
#include "Rendering/Resources/mesh.hpp"
#include <stdexcept>
#include <iostream>
#include <vector>


using namespace ECS;
namespace Rendering {

struct DepthBiasConfig {
    // Base bias values
    static constexpr float baseSlopeFactor = 1.75f;
    static constexpr float minConstantFactor = 0.001f;
    static constexpr float maxConstantFactor = 0.005f;
    
    // Scale factors
    static constexpr float lightDistanceScale = 0.5f;
    
    // Special case adjustments
    static constexpr float curvatureExtraFactor = 1.5f;
};

ShadowPass::ShadowPass(Device& device, CreateInfo& createInfo) 
    : device{device}{

    depthFormat = device.getDepthFormat();
    
    createRenderPass();
    createFramebuffers(createInfo);
    createPipelines(createInfo); 
}

ShadowPass::~ShadowPass() {
    cleanup();
}

void ShadowPass::cleanup() {

    vkDeviceWaitIdle(device.getDevice());
    
    cleanupFramebuffers(); 
    

    directionalLightPipeline.reset();
    spotLightPipeline.reset();
    pointLightPipeline.reset();
    

    if (directionalPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), directionalPipelineLayout, nullptr);
        directionalPipelineLayout = VK_NULL_HANDLE;
    }
    
    if (spotPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), spotPipelineLayout, nullptr);
        spotPipelineLayout = VK_NULL_HANDLE;
    }
    
    if (pointPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), pointPipelineLayout, nullptr);
        pointPipelineLayout = VK_NULL_HANDLE;
    }
     

    if (shadowRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.getDevice(), shadowRenderPass, nullptr);
        shadowRenderPass = VK_NULL_HANDLE;
    }


    std::cout << "Shadow pass cleaned up" << std::endl;
}

void ShadowPass::createRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 0;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthReference;


    std::array<VkSubpassDependency, 2> dependencies{};
    
    // Dependency 0: External -> Subpass 0
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;


    // Dependency 1: Subpass 0 -> External
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;


    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device.getDevice(), &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow render pass!");
    }
}


void ShadowPass::createPipelines(const CreateInfo& createInfo) {

  
    VkPushConstantRange directionalPushConstant{};
    directionalPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    directionalPushConstant.offset = 0;
    directionalPushConstant.size = sizeof(InstancedPushConstants);

    VkPushConstantRange spotPushConstant{};
    spotPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    spotPushConstant.offset = 0;
    spotPushConstant.size = sizeof(InstancedPushConstants);    

    VkPushConstantRange pointPushConstant{};
    pointPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pointPushConstant.offset = 0;
    pointPushConstant.size = sizeof(InstancedPushConstants);
    
    //Directional light layout
    std::array<VkDescriptorSetLayout, 3> setLayouts = {
        createInfo.lightMatrixDescriptorSetLayout,
        createInfo.shadowModelMatrixDescriptorSetLayout,
        createInfo.materialDescriptorSetLayout};

    VkPipelineLayoutCreateInfo directionalLayoutInfo{};
    directionalLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    directionalLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    directionalLayoutInfo.pSetLayouts = setLayouts.data();
    directionalLayoutInfo.pushConstantRangeCount = 1;
    directionalLayoutInfo.pPushConstantRanges = &directionalPushConstant;

    //Spot light layout
    VkPipelineLayoutCreateInfo spotLayoutInfo{};
    spotLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    spotLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    spotLayoutInfo.pSetLayouts = setLayouts.data();
    spotLayoutInfo.pushConstantRangeCount = 1;
    spotLayoutInfo.pPushConstantRanges = &spotPushConstant;

    //Point light layout
    VkPipelineLayoutCreateInfo pointLayoutInfo{};
    pointLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pointLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pointLayoutInfo.pSetLayouts = setLayouts.data();
    pointLayoutInfo.pushConstantRangeCount = 1;
    pointLayoutInfo.pPushConstantRanges = &pointPushConstant;

    //Create pipeline layouts
    if(vkCreatePipelineLayout(device.getDevice(), &directionalLayoutInfo, nullptr, &directionalPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instanced pipeline layout!");
    }

    if(vkCreatePipelineLayout(device.getDevice(), &spotLayoutInfo, nullptr, &spotPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instanced pipeline layout!");
    }

    if(vkCreatePipelineLayout(device.getDevice(), &pointLayoutInfo, nullptr, &pointPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instanced pipeline layout!");
    }
 
    // Create pipeline configuration
    PipelineConfigInfo pipelineConfig{};
    Pipeline::defaultPipelineConfigInfo(pipelineConfig);
    
    // Modify for shadow mapping specifics
    pipelineConfig.renderPass = shadowRenderPass;
    pipelineConfig.depthStencilInfo.depthTestEnable = VK_TRUE;
    pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
    pipelineConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    pipelineConfig.depthStencilInfo.minDepthBounds=0;
    pipelineConfig.depthStencilInfo.maxDepthBounds=1;
    pipelineConfig.rasterizationInfo.depthBiasEnable = VK_TRUE;
    pipelineConfig.rasterizationInfo.depthBiasConstantFactor = 1.75f;   
    pipelineConfig.rasterizationInfo.depthBiasSlopeFactor = 1.25f;
    pipelineConfig.rasterizationInfo.depthBiasClamp = 0.0f;
    pipelineConfig.rasterizationInfo.frontFace=VK_FRONT_FACE_CLOCKWISE;
    pipelineConfig.rasterizationInfo.cullMode=VK_CULL_MODE_BACK_BIT;
 

    pipelineConfig.bindingDescriptions = ShadowVertex::getBindingDescriptions();
    pipelineConfig.attributeDescriptions = ShadowVertex::getAttributeDescriptions();

    pipelineConfig.pipelineLayout = pointPipelineLayout;

    {
        std::vector<ShaderStageInfo> stages = {
            {VK_SHADER_STAGE_VERTEX_BIT, "shaders/shadowmap.vert.spv"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/shadowmap.frag.spv"}
        };
        pointLightPipeline = std::make_unique<Pipeline>(
            device,
            stages,
            pipelineConfig
        );
    }
    // For directional and spot light shadows
    pipelineConfig.pipelineLayout = directionalPipelineLayout;
    
    {
        std::vector<ShaderStageInfo> stages = {
            {VK_SHADER_STAGE_VERTEX_BIT, "shaders/shadowmap.vert.spv"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/shadowmap.frag.spv"}
        };
        directionalLightPipeline = std::make_unique<Pipeline>(
            device,
            stages,
            pipelineConfig
        );
    }

    pipelineConfig.pipelineLayout = spotPipelineLayout;

    {
        std::vector<ShaderStageInfo> stages = {
            {VK_SHADER_STAGE_VERTEX_BIT, "shaders/shadowmap.vert.spv"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/shadowmap.frag.spv"}
        };
        spotLightPipeline = std::make_unique<Pipeline>(
            device,
            stages,
            pipelineConfig
        );
    }

   
}


void ShadowPass::beginShadowRenderPass(
    VkCommandBuffer commandBuffer, 
    uint32_t frameIndex,
    size_t lightIndex, 
    LightType lightType,
    uint32_t layerIndex) {
    
    VkFramebuffer framebuffer;
    VkExtent2D extent;    
    switch(lightType) {
        case LightType::DIRECTIONAL_LIGHT:
            framebuffer = directionalFramebuffers[lightIndex][frameIndex][layerIndex];
            extent = {DIRECTIONAL_SHADOW_MAP_RES, DIRECTIONAL_SHADOW_MAP_RES};
            break;
        case LightType::POINT_LIGHT:
            framebuffer = pointFramebuffers[lightIndex][frameIndex][layerIndex];
            extent = {POINT_SHADOW_MAP_RES, POINT_SHADOW_MAP_RES};
            break;
        default: // Spot
            framebuffer = spotFramebuffers[lightIndex][frameIndex];
            extent = {SPOT_SHADOW_MAP_RES, SPOT_SHADOW_MAP_RES};
            break;
    }
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = shadowRenderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;

    VkClearValue clearValue;
    clearValue.depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void ShadowPass::endShadowRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

void ShadowPass::run(FrameContext& frameContext) {

    setBarriers(frameContext);
    
    if (frameContext.directionalShadowcastingMaterialMap.size() > 0) {
        renderDirectionalLights(frameContext);
    }
    
    if (frameContext.spotShadowcastingMaterialMap.size() > 0) {
        renderSpotLights(frameContext);
    }
    
    if (frameContext.pointShadowcastingMaterialMapByFace.size() > 0) {
        renderPointLights(frameContext);
    }
}

std::vector<VkVertexInputBindingDescription> ShadowPass::ShadowVertex::getBindingDescriptions() {
    std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Mesh::Vertex); // Still use full vertex stride since we're reusing the same vertex buffer
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescriptions;
}

std::vector<VkVertexInputAttributeDescription> ShadowPass::ShadowVertex::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);
    
    // Position attribute
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Mesh::Vertex, position);
    
    // UV attribute
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Mesh::Vertex, uv);
    
    return attributeDescriptions;
}


void ShadowPass::renderDirectionalLights(FrameContext& frameContext) {
    vkCmdBindPipeline(
        frameContext.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        directionalLightPipeline->getPipeline()
    );
    VkDescriptorSet lightMatrixDescriptorSet = frameContext.lightMatrixDescriptorSet;
    vkCmdBindDescriptorSets(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            directionalPipelineLayout,
            0,
            1,
            &lightMatrixDescriptorSet,
            0,
            nullptr
    );
    
    const auto& directionalMap = frameContext.directionalShadowcastingMaterialMap;

    uint32_t lightIndex = 0;

    for (auto& [directionalLight, cascadeBatches] : directionalMap) {

        VkDescriptorSet modelMatrixDescriptorSet = frameContext.shadowModelMatrixDescriptorSet;
        vkCmdBindDescriptorSets(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            directionalPipelineLayout,
            1,
            1,
            &modelMatrixDescriptorSet,
            0,
            nullptr
        );

        const uint32_t lightMatrixBase = frameContext.directionalLightMatrixBase.at(directionalLight);
        glm::vec4 lightPosRange = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);

        for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADE_COUNT; ++cascadeIndex) {
            const auto& materialBatches = cascadeBatches[cascadeIndex];
            if (materialBatches.empty()) {
                continue;
            }
            beginShadowRenderPass(frameContext.commandBuffer, frameContext.frameIndex, lightIndex, LightType::DIRECTIONAL_LIGHT, cascadeIndex);
            
            // Draw all batches in the current buffer update
            for (uint32_t i = 0; i < materialBatches.size(); i++) {
                const auto& materialBatch = materialBatches[i];
                
                InstancedPushConstants pushConstants{
                    lightPosRange,
                    lightMatrixBase + cascadeIndex,
                    materialBatch.matrixOffset,
                    0u // directional
                };
                
                vkCmdPushConstants(
                    frameContext.commandBuffer,
                    directionalPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(InstancedPushConstants),
                    &pushConstants
                );

                VkDescriptorSet materialDescriptorSet = materialBatch.material->getMaterialDescriptorSet();
                vkCmdBindDescriptorSets(
                    frameContext.commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    directionalPipelineLayout,
                    2,
                    1,
                    &materialDescriptorSet,
                    0,
                    nullptr
                );

                materialBatch.mesh->bind(frameContext.commandBuffer);
                materialBatch.mesh->drawSubmeshInstanced(frameContext.commandBuffer, materialBatch.submeshIndex, materialBatch.instanceCount);     
            }
            endShadowRenderPass(frameContext.commandBuffer);
        }
        lightIndex++;
    }
}

void ShadowPass::renderSpotLights(FrameContext& frameContext) {
    vkCmdBindPipeline(
        frameContext.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        spotLightPipeline->getPipeline()
    );

    VkDescriptorSet lightMatrixDescriptorSet = frameContext.lightMatrixDescriptorSet;
    vkCmdBindDescriptorSets(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            spotPipelineLayout,
            0,
            1,
            &lightMatrixDescriptorSet,
            0,
            nullptr
    );
    
    const auto& spotMap = frameContext.spotShadowcastingMaterialMap;
    uint32_t lightIndex = 0;
    for (auto& [spotLightPtr, materialBatches] : spotMap) {

        SpotLight& spotLight = *spotLightPtr;
        glm::vec3 lightPos = spotLight.transform.position;
        float range = spotLight.range;
        glm::vec4 lightPosRange = glm::vec4(lightPos, range);
        
        beginShadowRenderPass(frameContext.commandBuffer, frameContext.frameIndex, lightIndex, LightType::SPOT_LIGHT);

        VkDescriptorSet modelMatrixDescriptorSet = frameContext.shadowModelMatrixDescriptorSet;
        vkCmdBindDescriptorSets(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            spotPipelineLayout,
            1,
            1,
            &modelMatrixDescriptorSet,
            0,
            nullptr
        );
            
            // Draw all batches in the current buffer update
        for (uint32_t i = 0; i < materialBatches.size(); i++) {
            const auto& materialBatch = materialBatches[i];
                
            const uint32_t lightMatrixBase = frameContext.spotLightMatrixBase.at(spotLightPtr);
            InstancedPushConstants pushConstants{
                lightPosRange,
                lightMatrixBase,
                materialBatch.matrixOffset,
                1u // spot
            };
                
            vkCmdPushConstants(
                frameContext.commandBuffer,
                spotPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(InstancedPushConstants),
                &pushConstants
            );

            VkDescriptorSet materialDescriptorSet = materialBatch.material->getMaterialDescriptorSet();
            vkCmdBindDescriptorSets(
                frameContext.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                spotPipelineLayout,
                2,
                1,
                &materialDescriptorSet,
                0,
                nullptr
            );

            materialBatch.mesh->bind(frameContext.commandBuffer);
            materialBatch.mesh->drawSubmeshInstanced(frameContext.commandBuffer, materialBatch.submeshIndex, materialBatch.instanceCount);
        }
            
        endShadowRenderPass(frameContext.commandBuffer);
        
        
        lightIndex++;
    }
}

void ShadowPass::renderPointLights(FrameContext& frameContext) {
    vkCmdBindPipeline(
        frameContext.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pointLightPipeline->getPipeline()
    );


    VkDescriptorSet lightMatrixDescriptorSet = frameContext.lightMatrixDescriptorSet;
    vkCmdBindDescriptorSets(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pointPipelineLayout,
            0,
            1,
            &lightMatrixDescriptorSet,
            0,
            nullptr
    );
    
    const auto& pointMap = frameContext.pointShadowcastingMaterialMapByFace;
    
    uint32_t lightIndex = 0;
    
    for (auto& [pointLightPtr, faceBatches] : pointMap) {
        PointLight& pointLight = *pointLightPtr;
        glm::vec3 lightPos = pointLight.transform.position;
        float range = pointLight.range;
        glm::vec4 lightPosRange = glm::vec4(lightPos, range);
        const uint32_t lightMatrixBase = frameContext.pointLightMatrixBase.at(pointLightPtr);

        VkDescriptorSet modelMatrixDescriptorSet = frameContext.shadowModelMatrixDescriptorSet;
        vkCmdBindDescriptorSets(
            frameContext.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pointPipelineLayout,
            1,
            1,
            &modelMatrixDescriptorSet,
            0,
            nullptr
        );

        for (uint32_t face = 0; face < 6; ++face) {
            const auto& materialBatches = faceBatches[face];
            if (materialBatches.empty()) {
                continue;
            }
            beginShadowRenderPass(frameContext.commandBuffer, frameContext.frameIndex, lightIndex, LightType::POINT_LIGHT, face);

            // Draw all batches in the current buffer update
            for (uint32_t i = 0; i < materialBatches.size(); i++) {
                const auto& materialBatch = materialBatches[i];
                
                InstancedPushConstants pushConstants{
                    lightPosRange,
                    lightMatrixBase + face,
                    materialBatch.matrixOffset,
                    2u // point
                };
                
                vkCmdPushConstants(
                    frameContext.commandBuffer,
                    pointPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(InstancedPushConstants),
                    &pushConstants
                );

                VkDescriptorSet materialDescriptorSet = materialBatch.material->getMaterialDescriptorSet();
                vkCmdBindDescriptorSets(
                    frameContext.commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pointPipelineLayout,
                    2,
                    1,
                    &materialDescriptorSet,
                    0,
                    nullptr
                );

                materialBatch.mesh->bind(frameContext.commandBuffer);
                materialBatch.mesh->drawSubmeshInstanced(frameContext.commandBuffer, materialBatch.submeshIndex, materialBatch.instanceCount);
            }
            
            endShadowRenderPass(frameContext.commandBuffer);
        }

        lightIndex++;
    }
}
// Helper method to update instance buffers from DrawingData


void ShadowPass::setBarriers(FrameContext& frameContext) {
    VkCommandBuffer commandBuffer = frameContext.commandBuffer;
    
    // Create barriers for the shadow uniform buffer and instance model matrix buffer
    std::array<VkBufferMemoryBarrier, 2> bufferBarriers{};
    
    // Shadow uniform buffer barrier - contains light matrices
    bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarriers[0].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    bufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bufferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[0].buffer = frameContext.lightMatrixBuffer->getBuffer();
    bufferBarriers[0].offset = 0;
    bufferBarriers[0].size = VK_WHOLE_SIZE;
    
    // Instance model matrix buffer barrier
    bufferBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarriers[1].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    bufferBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bufferBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarriers[1].buffer = frameContext.shadowModelMatrixBuffer->getBuffer();
    bufferBarriers[1].offset = 0;
    bufferBarriers[1].size = VK_WHOLE_SIZE;
    

    // Issue all barriers at once
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
        0, 
        0, nullptr, 
        static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(), 
        0, nullptr 
    );
}



void ShadowPass::updateMatrixBufferDescriptorSets(FrameContext& frameContext) {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkDescriptorBufferInfo modelBufferInfo = frameContext.shadowModelMatrixBuffer->descriptorInfo();        

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = frameContext.shadowModelMatrixDescriptorSet;
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &modelBufferInfo;
                
            vkUpdateDescriptorSets(
                device.getDevice(),
                1,
                &write,
                0, nullptr
            );
        }
    }

void ShadowPass::createFramebuffers(const CreateInfo& createInfo) {
    // Create framebuffers for directional lights (one per cascade)
    for (size_t lightIndex = 0; lightIndex < MAX_DIRECTIONAL_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            auto& shadowMap = (*createInfo.directionalShadowMaps)[lightIndex][frameIndex];
            for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADE_COUNT; ++cascadeIndex) {
                createShadowFramebuffer(
                    shadowMap->getLayerImageView(cascadeIndex),
                    DIRECTIONAL_SHADOW_MAP_RES, DIRECTIONAL_SHADOW_MAP_RES, 1,
                    directionalFramebuffers[lightIndex][frameIndex][cascadeIndex]
                );
            }
        }
    }

    // Create framebuffers for spot lights
    for (size_t lightIndex = 0; lightIndex < MAX_SPOT_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            auto& shadowMap = (*createInfo.spotShadowMaps)[lightIndex][frameIndex];
            createShadowFramebuffer(
                shadowMap->getImageView(),
                SPOT_SHADOW_MAP_RES, SPOT_SHADOW_MAP_RES, 1,
                spotFramebuffers[lightIndex][frameIndex]
            );
        }
    }

    // Create framebuffers for point lights (one per face)
    for (size_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            auto& shadowMap = (*createInfo.pointShadowMaps)[lightIndex][frameIndex];
            for (uint32_t face = 0; face < 6; ++face) {
                createShadowFramebuffer(
                    shadowMap->getLayerImageView(face),
                    POINT_SHADOW_MAP_RES, POINT_SHADOW_MAP_RES, 1,
                    pointFramebuffers[lightIndex][frameIndex][face]
                );
            }
        }
    }
}

void ShadowPass::cleanupFramebuffers() {
    // Clean up directional light framebuffers
    for (auto& frameBufferArray : directionalFramebuffers) {
        for (auto& framebufferPerFrame : frameBufferArray) {
            for (auto& framebuffer : framebufferPerFrame) {
                if (framebuffer != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(device.getDevice(), framebuffer, nullptr);
                    framebuffer = VK_NULL_HANDLE;
                }
            }
        }
    }

    // Clean up spot light framebuffers
    for (auto& frameBufferArray : spotFramebuffers) {
        for (auto& framebuffer : frameBufferArray) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device.getDevice(), framebuffer, nullptr);
                framebuffer = VK_NULL_HANDLE;
            }
        }
    }

    // Clean up point light framebuffers
    for (auto& frameBufferArray : pointFramebuffers) {
        for (auto& framebufferPerFrame : frameBufferArray) {
            for (auto& framebuffer : framebufferPerFrame) {
                if (framebuffer != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(device.getDevice(), framebuffer, nullptr);
                    framebuffer = VK_NULL_HANDLE;
                }
            }
        }
    }
}

void ShadowPass::createShadowFramebuffer(
    VkImageView imageView,
    uint32_t width,
    uint32_t height,
    uint32_t layers,
    VkFramebuffer& framebuffer) {
    
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = shadowRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &imageView;
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = layers;

    if (vkCreateFramebuffer(device.getDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow framebuffer!");
    }
}

} // namespace Rendering