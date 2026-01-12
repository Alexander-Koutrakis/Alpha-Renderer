#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "Rendering/Resources/mesh.hpp"
#include "Rendering/Core/descriptors.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/Core/frame_context.hpp"
#include "Rendering/rendering_constants.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/Resources/gbuffer.hpp"
#include "Rendering/RenderPasses/Geometry/geometry_pass.hpp"
#include "Scene/enviroment_lighting.hpp"
#include "Rendering/Resources/texture.hpp"

#include "ECS/ecs.hpp"
#include <memory>
#include <array>

namespace ECS {
    struct SkyboxComponent;
}

namespace Rendering{

    class SkyboxPass{
        public:
            struct CreateInfo {
                uint32_t width;
                uint32_t height;
                VkDescriptorSetLayout cameraDescriptorSetLayout;
                VkDescriptorSetLayout skyboxDescriptorSetLayout;
                VkFormat albedoFormat;
                std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>* albedoViewsPtr;
                std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>* depthViewsPtr;
            };

            SkyboxPass(
                Device& device,
                const CreateInfo& createInfo);
            ~SkyboxPass();
            void run(FrameContext& frameContext);
        private:
            void cleanup();
            void createRenderPass(const CreateInfo& createInfo);
            void createPipeline(const CreateInfo& createInfo);
            void createFramebuffers(const CreateInfo& createInfo);

            void beginRenderPass(FrameContext& frameContext);
            void endRenderPass(FrameContext& frameContext);
            void setBarriers(FrameContext& frameContext);            
            Device& device;
            VkRenderPass renderPass{VK_NULL_HANDLE}; 
            std::unique_ptr<Pipeline> pipeline{nullptr};
            VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
            uint32_t width;
            uint32_t height;


            // Framebuffers for rendering
            std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers{};

            
    };
}

