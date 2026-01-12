#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/Core/window.hpp"
#include "Rendering/RenderPasses/Shadowmapping/shadow_pass.hpp"
#include "Rendering/RenderPasses/Geometry/geometry_pass.hpp"
#include "Rendering/RenderPasses/General/skybox_pass.hpp"
#include "Rendering/RenderPasses/Direct Lighting/light_pass.hpp"
#include "Rendering/RenderPasses/Transparency/transparency_pass.hpp"
#include "Rendering/RenderPasses/Composition/composition_pass.hpp"
#include "Rendering/RenderPasses/Radiance Cascades/rc_gi_pass.hpp"
#include "Rendering/RenderPasses/SMAA/smaa_edge_pass.hpp"
#include "Rendering/RenderPasses/SMAA/smaa_weight_pass.hpp"
#include "Rendering/RenderPasses/SMAA/smaa_blend_pass.hpp"
#include "Rendering/RenderPasses/Color Correction/color_correction_pass.hpp"
#include "Rendering/Resources/gbuffer.hpp"
#include "Rendering/Core/imgui_manager.hpp"
#include "Systems/camera_system.hpp"
#include "Systems/camera_culling.hpp"
#include "Systems/light_system.hpp"
#include "Rendering/rendering_constants.hpp"
#include "ECS/components.hpp"
// std
#include <cassert>
#include <memory>
#include <vector>


namespace Rendering {
    class Renderer {
    public:
        Renderer(Window& window, Device& device);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // Frame management
        VkCommandBuffer beginFrame();
        void endFrame();
        bool isFrameInProgress() const { return isFrameStarted; }
        bool isFramebufferResized() const { return framebufferResized; }
        void setFramebufferResized(bool value) { framebufferResized = value; }
        
        // Command buffer and frame info accessors
        VkCommandBuffer getCurrentCommandBuffer() const {
            assert(isFrameStarted && "Cannot get command buffer when frame not in progress");
            return commandBuffers[currentFrameIndex];
        }
        
        uint32_t getCurrentImageIndex() const {
            assert(isFrameStarted && "Cannot get frame index when frame not in progress");
            return currentImageIndex;
        }

        size_t getFrameIndexCount() const {
            return commandBuffers.size();
        }

        // Swap chain access for image views and format
        VkFormat getSwapChainImageFormat() const { return swapChain->getSwapChainImageFormat(); }   
        float getAspectRatio() const { return swapChain->extentAspectRatio(); }
        VkExtent2D getSwapChainExtent() const { return swapChain->getExtent(); }
        
        void run();
        
        // ImGui access
        ImGuiManager* getImGuiManager() { return imguiManager.get(); }
        
    private:
        void recreateSwapChain();
        void cleanupWindowDependentResources();
        void recreateWindowDependentResources();
        void handleWindowResize();
        void createCommandBuffers();
        void freeCommandBuffers();

        void createRenderingResources();
        void createShadowPass();
        void createGeometryPass();
        void createSkyboxPass();
        void createTransparencyPass();
        void createLightPass();
        void createRCGIPass();
        void createCompositionPass();
        void createSMAAPasses();
        void createColorCorrectionPass();
        void updateFrameContext(VkCommandBuffer commandBuffer, FrameContext& frameContext);
        Window& window;
        Device& device;
        std::shared_ptr<SwapChain> swapChain;
        std::vector<VkCommandBuffer> commandBuffers;
        std::unique_ptr<RenderingResources> renderingResources;
        std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frameContexts;
        std::unique_ptr<GBuffer> gBuffer;
        std::unique_ptr<GeometryPass> geometryPass;
        std::unique_ptr<TransparencyPass> transparencyPass;
        std::unique_ptr<ShadowPass> shadowmapPass;
        std::unique_ptr<SkyboxPass> skyboxPass;
        std::unique_ptr<LightPass> lightPass;
        std::unique_ptr<RCGIPass> rcgiPass;
        std::unique_ptr<CompositionPass> compositionPass;
        std::unique_ptr<SMAAEdgePass> smaaEdgePass;
        std::unique_ptr<SMAAWeightPass> smaaWeightPass;
        std::unique_ptr<SMAABlendPass> smaaBlendPass;
        std::unique_ptr<ColorCorrectionPass> colorCorrectionPass;

        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> swapchainImageViews{};
        std::unique_ptr<ImGuiManager> imguiManager;

        uint32_t currentImageIndex{0};
        size_t currentFrameIndex{0};
        bool isFrameStarted{false};
        bool framebufferResized{false};
        
        // Temporal accumulation: track previous frame camera data
        glm::mat4 prevViewProjMatrix{1.0f};
        uint32_t temporalFrameCounter{0};
        bool hasPreviousFrame{false};  // First frame has no valid history
    };
}