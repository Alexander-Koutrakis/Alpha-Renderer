
#pragma once

#include "Rendering/Core/device.hpp"
#include "shadow_map.hpp"
#include "Rendering/Core/frame_context.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/RenderPasses/Geometry/geometry_pass.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "Rendering/Core/descriptors.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Systems/bounding_box_system.hpp"
#include "Rendering/rendering_constants.hpp"
#include "ECS/ecs.hpp"
#include "ECS/components.hpp"
#include "ECS/ecs_types.hpp"
#include <memory>
#include <array>
#include <unordered_map>

using namespace ECS;
namespace Rendering {

class ShadowPass {
public:
    

    struct PushConstants {
        alignas(16)uint32_t matrixIndex;
        alignas(16)glm::mat4 modelMatrix;
        alignas(16)glm::vec4 lightPosRange;
        PushConstants(uint32_t matrixIndex, glm::mat4 modelMatrix, glm::vec4 lightPosRange) 
            : matrixIndex(matrixIndex), modelMatrix(modelMatrix), lightPosRange(lightPosRange) {};
    };

    struct InstancedPushConstants {
        glm::vec4 lightPosRange;
        uint32_t lightMatrixIndex;    
        uint32_t modelMatrixOffset;       
        uint32_t lightType;               
        InstancedPushConstants(glm::vec4 lightPosRange, uint32_t lightMatrixIndex, uint32_t modelMatrixOffset, uint32_t lightType) 
            : lightPosRange(lightPosRange), lightMatrixIndex(lightMatrixIndex), modelMatrixOffset(modelMatrixOffset), lightType(lightType){};
    };

    struct ShadowVertex {
        glm::vec3 position;
        glm::vec2 uv;
        
        static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
    };

    struct CreateInfo{
        VkDescriptorSetLayout lightMatrixDescriptorSetLayout;
        VkDescriptorSetLayout shadowModelMatrixDescriptorSetLayout;
        VkDescriptorSetLayout materialDescriptorSetLayout;

        std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_DIRECTIONAL_LIGHTS>* directionalShadowMaps;
        std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_POINT_LIGHTS>* pointShadowMaps;
        std::array<std::array<std::unique_ptr<ShadowMap>, MAX_FRAMES_IN_FLIGHT>, MAX_SPOT_LIGHTS>* spotShadowMaps;
    };

 

    ShadowPass(Device& device,CreateInfo& createInfo);
    ~ShadowPass();

    ShadowPass(const ShadowPass&) = delete;
    ShadowPass& operator=(const ShadowPass&) = delete;

    void run(FrameContext& frameContext);
private:
    // Resource management functions
    void cleanup();
    void createRenderPass();
    void createPipelines(const CreateInfo& createInfo);
    void createFramebuffers(const CreateInfo& createInfo);
    void cleanupFramebuffers();
    void createShadowFramebuffer(
        VkImageView imageView,
        uint32_t width,
        uint32_t height,
        uint32_t layers,
        VkFramebuffer& framebuffer
    );


    void setBarriers(FrameContext& frameContext);
    // Rendering functions
    void renderDirectionalLights(FrameContext& frameContext);
    void renderPointLights(FrameContext& frameContext);
    void renderSpotLights(FrameContext& frameContext);
    void beginShadowRenderPass(
        VkCommandBuffer commandBuffer, 
        uint32_t frameIndex,
        size_t lightIndex, 
        LightType lightType,
        uint32_t layerIndex = 0
        );

    void endShadowRenderPass(VkCommandBuffer commandBuffer);

    void updateMatrixBufferDescriptorSets(FrameContext& frameContext);

    Device& device;
    VkRenderPass shadowRenderPass{VK_NULL_HANDLE};
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    
    // Pipeline and layout resources for instanced rendering
    std::unique_ptr<Pipeline> directionalLightPipeline;
    std::unique_ptr<Pipeline> spotLightPipeline;
    std::unique_ptr<Pipeline> pointLightPipeline;
    VkPipelineLayout directionalPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout spotPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout pointPipelineLayout{VK_NULL_HANDLE};
    std::array<std::array<std::array<VkFramebuffer, MAX_SHADOW_CASCADE_COUNT>, MAX_FRAMES_IN_FLIGHT>, MAX_DIRECTIONAL_LIGHTS> directionalFramebuffers{};
    std::array<std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT>, MAX_SPOT_LIGHTS> spotFramebuffers{};
    std::array<std::array<std::array<VkFramebuffer, 6>, MAX_FRAMES_IN_FLIGHT>, MAX_POINT_LIGHTS> pointFramebuffers{};    
    
};

} // namespace Rendering