#pragma once
#include "Rendering/Core/device.hpp"
#include <array>
#include <vector>
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/rendering_constants.hpp"
#include <stdexcept>
#include <iostream>
namespace Rendering {

class GBuffer {
public:

    static constexpr uint32_t ATTACHMENT_COUNT = 4;

    struct CreateInfo {
        uint32_t width;
        uint32_t height;
    };

    GBuffer(Device& device, const CreateInfo& createInfo);
    ~GBuffer();

    VkImageView getPositionView(size_t frameIndex) const { return positionViews[frameIndex]; }
    VkImageView getNormalView(size_t frameIndex) const { return normalViews[frameIndex]; }
    VkImageView getAlbedoView(size_t frameIndex) const { return albedoViews[frameIndex]; }
    VkImageView getMaterialView(size_t frameIndex) const { return materialViews[frameIndex]; }

    VkImage getPositionImage(size_t frameIndex) const { return positionImages[frameIndex]; }
    VkImage getNormalImage(size_t frameIndex) const { return normalImages[frameIndex]; }
    VkImage getAlbedoImage(size_t frameIndex) const { return albedoImages[frameIndex]; }
    VkImage getMaterialImage(size_t frameIndex) const { return materialImages[frameIndex]; }

    std::array<VkImageView, ATTACHMENT_COUNT> getAttachmentViews(size_t frameIndex) const {
        return {positionViews[frameIndex], normalViews[frameIndex], 
                albedoViews[frameIndex], materialViews[frameIndex]};
    }

    std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getPositionViews() { return positionViews; }
    std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getNormalViews() { return normalViews; }
    std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getAlbedoViews() { return albedoViews; }
    std::array<VkImageView,MAX_FRAMES_IN_FLIGHT>& getMaterialViews() { return materialViews; }

    VkSampler getSampler() const { return sampler; }
    VkFormat getPositionFormat() const { return positionFormat; }
    VkFormat getNormalFormat() const { return normalFormat; }
    VkFormat getAlbedoFormat() const { return albedoFormat; }
    VkFormat getMaterialFormat() const { return materialFormat; }
private:
    void setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name);
    void cleanup();
    void findResourcesFormats();
    void createAttachments();
    void createAttachment(
        VkFormat format,
        VkImageUsageFlags usage,
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT>& images,
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT>& memories,
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& views,
        const std::string& name);
    Device& device;
    uint32_t width;
    uint32_t height;

    VkFormat positionFormat{VK_FORMAT_UNDEFINED};
    VkFormat normalFormat{VK_FORMAT_UNDEFINED};
    VkFormat albedoFormat{VK_FORMAT_UNDEFINED};
    VkFormat materialFormat{VK_FORMAT_UNDEFINED};
    // Position buffer (RGB32F)
    std::array<VkImage, MAX_FRAMES_IN_FLIGHT> positionImages{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> positionMemories{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> positionViews{};
    // Normal buffer (RGB16F)
    std::array<VkImage, MAX_FRAMES_IN_FLIGHT> normalImages{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> normalMemories{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> normalViews{};
    // Albedo buffer (RGBA8)
    std::array<VkImage, MAX_FRAMES_IN_FLIGHT> albedoImages{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> albedoMemories{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> albedoViews{};
    // Material properties buffer (RGBA8)
    std::array<VkImage, MAX_FRAMES_IN_FLIGHT> materialImages{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> materialMemories{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> materialViews{};
    VkSampler sampler{VK_NULL_HANDLE};
    void createSampler();
};

}