#pragma once

#include "device.hpp"
#include "window.hpp"
#include "swapchain.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <memory>

namespace Rendering {

/**
 * @brief Manages Dear ImGui initialization, rendering, and cleanup for Vulkan
 * 
 * This class encapsulates all ImGui-related Vulkan resources including:
 * - Descriptor pool for ImGui
 * - Render pass for ImGui rendering
 * - Integration with GLFW for input handling
 * - Integration with Vulkan for rendering
 */
class ImGuiManager {
public:
    /**
     * @brief Initialize ImGui with Vulkan and GLFW
     * @param device Reference to the Vulkan device wrapper
     * @param window Reference to the GLFW window wrapper
     * @param swapChain Reference to the swap chain
     * @param imageCount Number of swap chain images
     */
    ImGuiManager(Device& device, Window& window, SwapChain& swapChain, uint32_t imageCount);
    
    /**
     * @brief Cleanup ImGui resources
     */
    ~ImGuiManager();

    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    /**
     * @brief Main run function - renders all ImGui UI elements
     * @param commandBuffer The command buffer to record ImGui draw commands into
     * @param imageIndex The current swap chain image index
     */
    void run(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    /**
     * @brief Set frame statistics for display
     * @param fps Current frames per second
     * @param frameTime Current frame time in milliseconds
     */
    void setFrameStats(float fps, float frameTime);

    /**
     * @brief Handle window resize
     * @param swapChain Reference to the new swap chain after resize
     */
    void onWindowResize(SwapChain& swapChain);

    VkRenderPass getRenderPass() const { return imguiRenderPass; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return framebuffers; }

private:
    void createDescriptorPool();
    void createRenderPass(SwapChain& swapChain);
    void createFramebuffers(SwapChain& swapChain);
    void initImGui(Window& window, uint32_t imageCount);
    void cleanup();
    
    void beginFrame();
    void endFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void renderFPSCounter();

    Device& device;
    VkDescriptorPool imguiDescriptorPool{VK_NULL_HANDLE};
    VkRenderPass imguiRenderPass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers;
    bool initialized{false};
    
    // Frame statistics
    float currentFPS{0.0f};
    float currentFrameTime{0.0f};
};

} // namespace Rendering

