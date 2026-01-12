#pragma once

#include "window.hpp"

// std lib headers
#include <string>
#include <vector>
#include <ktxvulkan.h>

namespace Rendering {

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct QueueFamilyIndices {
        uint32_t graphicsFamily;
        uint32_t presentFamily;
        bool graphicsFamilyHasValue = false;
        bool presentFamilyHasValue = false;
        bool isComplete() { return graphicsFamilyHasValue && presentFamilyHasValue; }
    };

    struct KTXVulkanDeviceInfo {
        VkDevice device;
        VkQueue queue;
        VkCommandPool cmdPool;
        VkPhysicalDevice physicalDevice;
        const VkAllocationCallbacks* pAllocator;
    };

    class Device {
    public:

        const bool enableValidationLayers = false;

        Device(Window& window);
        ~Device();

        // Not copyable or movable
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;
        Device(Device&&) = delete;
        Device& operator=(Device&&) = delete;

        VkCommandPool getCommandPool() { return commandPool; }
        VkDevice getDevice() { return device_; }
        VkSurfaceKHR getSurface() { return surface_; }
        VkQueue getGraphicsQueue() { return graphicsQueue_; }
        VkQueue getPresentQueue() { return presentQueue_; }
        VkPhysicalDevice getPhysicalDevice(){return physicalDevice;}
        VkInstance getInstance() { return instance; }
        
        SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(physicalDevice); }
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice); }
        VkFormat findSupportedFormat(
            const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

        // Buffer Helper Functions
        void createBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties,
            VkBuffer& buffer,
            VkDeviceMemory& bufferMemory);
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void copyBufferToImage(
            VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);

        void createImageWithInfo(
            const VkImageCreateInfo& imageInfo,
            VkMemoryPropertyFlags properties,
            VkImage& image,
            VkDeviceMemory& imageMemory);

        // Check if the device supports the given compressed format
        bool supportsTextureCompression(VkFormat format);
        
        // Helper to find the best supported compressed format
        VkFormat findBestCompressedFormat(bool srgb = true, bool hasAlpha = true);

        VkPhysicalDeviceProperties deviceProperties;
        VkFormat getDepthFormat();

        ktxVulkanDeviceInfo getVulkanUploadContext() const {
            ktxVulkanDeviceInfo uploadContext{};
            uploadContext.device = device_;
            uploadContext.queue = graphicsQueue_;
            uploadContext.cmdPool = commandPool;
            uploadContext.physicalDevice = physicalDevice;
            uploadContext.pAllocator = nullptr;
            return uploadContext;
        }

    private:
        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createCommandPool();

        // helper functions
        bool isDeviceSuitable(VkPhysicalDevice device);
        std::vector<const char*> getRequiredExtensions();
        bool checkValidationLayerSupport();
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
        void hasGflwRequiredInstanceExtensions();
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        Window& window;
        VkCommandPool commandPool;

        VkDevice device_;
        VkSurfaceKHR surface_;
        VkQueue graphicsQueue_;
        VkQueue presentQueue_;

        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
        const std::vector<const char*> deviceExtensions = { 
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME
        };
    };

}  // namespace lve