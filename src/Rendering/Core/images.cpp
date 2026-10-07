#include "images.hpp"
#include <stdexcept>

namespace Rendering {

VkImageCreateInfo imageCreateInfo2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                                    uint32_t mipLevels, uint32_t arrayLayers, VkImageCreateFlags flags,
                                    VkImageTiling tiling) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = flags;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = mipLevels;
    info.arrayLayers = arrayLayers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = tiling;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return info;
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageViewType viewType,
                            VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount,
                            uint32_t baseArrayLayer, uint32_t layerCount) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = viewType;
    info.format = format;
    info.subresourceRange = {aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount};

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }
    return view;
}

} // namespace Rendering
