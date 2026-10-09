#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/rendering_constants.hpp"
#include <array>
#include <string>
#include <vector>

namespace Rendering {

// One 2D image, its memory and a view over all its mips for each frame in flight, plus optional per-mip views.
// Device-local, single-sampled, optimal tiling, one array layer. Destroys everything it created.
class FrameImage {
public:
    struct CreateInfo {
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkImageUsageFlags usage{0};
        VkExtent2D extent{};
        uint32_t mipLevels{1};
        VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
        bool perMipViews{false}; // one view per mip in addition to the full view
        // Debug names: <name>Image_Frame<i>, <name>View_Frame<i>, <name>Memory_Frame<i>, <name>MipView_Frame<i>_Mip<m>
        std::string name;
    };

    FrameImage(Device& device, const CreateInfo& createInfo);
    ~FrameImage();

    FrameImage(const FrameImage&) = delete;
    FrameImage& operator=(const FrameImage&) = delete;

    VkImage image(size_t frame) const { return images[frame]; }
    VkImageView view(size_t frame) const { return views_[frame]; }
    // The view of the frame before `frame`, for history reads.
    VkImageView previousView(size_t frame) const {
        return views_[(frame + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT];
    }
    VkImageView mipView(size_t frame, uint32_t mip) const { return mipViews_[frame][mip]; }
    const std::vector<VkImageView>& mipViews(size_t frame) const { return mipViews_[frame]; }
    const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& views() const { return views_; }

    VkFormat format() const { return format_; }
    VkExtent2D extent() const { return extent_; }
    uint32_t mipLevels() const { return mipLevels_; }

private:
    Device& device;
    VkFormat format_{VK_FORMAT_UNDEFINED};
    VkExtent2D extent_{};
    uint32_t mipLevels_{1};
    std::array<VkImage, MAX_FRAMES_IN_FLIGHT> images{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> memories{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> views_{};
    std::array<std::vector<VkImageView>, MAX_FRAMES_IN_FLIGHT> mipViews_{};
};

} // namespace Rendering
