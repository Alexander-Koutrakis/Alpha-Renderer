#pragma once

#include "core.hpp"

namespace Rendering {

// Create-info for a single-sampled, exclusive-sharing 2D image in UNDEFINED layout, the shape every image in the
// renderer has. Pass it to Device::createImageWithInfo.
VkImageCreateInfo imageCreateInfo2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                                    uint32_t mipLevels = 1, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0,
                                    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);

// Creates an image view over the given subresource range; throws std::runtime_error on failure.
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format,
                            VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
                            VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t baseMipLevel = 0,
                            uint32_t levelCount = 1, uint32_t baseArrayLayer = 0, uint32_t layerCount = 1);

} // namespace Rendering
