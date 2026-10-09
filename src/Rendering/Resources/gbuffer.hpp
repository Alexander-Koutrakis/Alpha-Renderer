#pragma once
#include "Rendering/Core/device.hpp"
#include "Rendering/Resources/frame_image.hpp"
#include "Rendering/rendering_constants.hpp"
#include <array>
#include <memory>

namespace Rendering {

class GBuffer {
public:
    struct CreateInfo {
        uint32_t width;
        uint32_t height;
    };

    GBuffer(Device& device, const CreateInfo& createInfo);
    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    const FrameImage& position() const { return *positionImage; }
    const FrameImage& normal() const { return *normalImage; }

    VkImageView getPositionView(size_t frameIndex) const { return positionImage->view(frameIndex); }
    VkImageView getNormalView(size_t frameIndex) const { return normalImage->view(frameIndex); }
    VkImageView getAlbedoView(size_t frameIndex) const { return albedoImage->view(frameIndex); }
    VkImageView getMaterialView(size_t frameIndex) const { return materialImage->view(frameIndex); }

    VkImage getPositionImage(size_t frameIndex) const { return positionImage->image(frameIndex); }
    VkImage getNormalImage(size_t frameIndex) const { return normalImage->image(frameIndex); }
    VkImage getAlbedoImage(size_t frameIndex) const { return albedoImage->image(frameIndex); }
    VkImage getMaterialImage(size_t frameIndex) const { return materialImage->image(frameIndex); }

    const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& getAlbedoViews() const { return albedoImage->views(); }

    VkSampler getSampler() const { return sampler; }
    VkFormat getPositionFormat() const { return positionImage->format(); }
    VkFormat getNormalFormat() const { return normalImage->format(); }
    VkFormat getAlbedoFormat() const { return albedoImage->format(); }
    VkFormat getMaterialFormat() const { return materialImage->format(); }

private:
    void createAttachments();
    void createSampler();

    Device& device;
    uint32_t width;
    uint32_t height;

    std::unique_ptr<FrameImage> positionImage; // RGB32F
    std::unique_ptr<FrameImage> normalImage;   // RGB10A2 or RGB16F
    std::unique_ptr<FrameImage> albedoImage;   // RGBA8
    std::unique_ptr<FrameImage> materialImage; // RGBA8, PBR properties
    VkSampler sampler{VK_NULL_HANDLE};
};

} // namespace Rendering
