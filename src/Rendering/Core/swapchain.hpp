#pragma once

#include "device.hpp"
#include "core.hpp"
#include <memory>
#include <vector>
#include "Rendering/rendering_constants.hpp"
namespace Rendering {

class SwapChain {
public:
    SwapChain(Device& deviceRef, VkExtent2D windowExtent);
    SwapChain(Device& deviceRef, VkExtent2D windowExtent, std::shared_ptr<SwapChain> previous);
    ~SwapChain();

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    VkImageView getImageView(size_t index) { return swapChainImageViews[index]; }
    std::vector<VkImageView>& getImageViews() { return swapChainImageViews; }
    VkExtent2D getExtent() { return swapChainExtent; }
    VkFormat getSwapChainImageFormat() const { return swapChainImageFormat; }
    size_t imageCount() const { return swapChainImages.size(); }

    float extentAspectRatio() {
        return static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
    }

    // frameIndex is the frame-in-flight slot (0..MAX_FRAMES_IN_FLIGHT-1), owned by the caller.
    VkResult acquireNextImage(uint32_t frameIndex, uint32_t* imageIndex);
    VkResult submitCommandBuffers(uint32_t frameIndex, const VkCommandBuffer* buffers, uint32_t* imageIndex);

    bool compareSwapFormats(const SwapChain& other) const { return other.swapChainImageFormat == swapChainImageFormat; }

private:
    void init();
    void createSwapChain();
    void createImageViews();
    void createSyncObjects();

    // Helper functions
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    VkFormat swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapChainExtent{};

    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;

    Device& device;
    VkExtent2D windowExtent{};

    VkSwapchainKHR vkSwapChain = VK_NULL_HANDLE;
    std::shared_ptr<SwapChain> oldSwapChain;

    // Per frame-in-flight slot.
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkFence> inFlightFences;
    // Per swapchain image: a present operation holds its semaphore until the image is acquired again.
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> imagesInFlight;
};
} // namespace Rendering