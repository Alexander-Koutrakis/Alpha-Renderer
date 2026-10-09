#include "frame_image.hpp"
#include "Rendering/Core/debug_names.hpp"
#include "Rendering/Core/images.hpp"

namespace Rendering {

FrameImage::FrameImage(Device& device, const CreateInfo& createInfo)
    : device{device}, format_{createInfo.format}, extent_{createInfo.extent}, mipLevels_{createInfo.mipLevels} {
    const VkImageCreateInfo imageInfo =
        imageCreateInfo2D(extent_.width, extent_.height, format_, createInfo.usage, mipLevels_);
    const VkDevice vkDevice = device.getDevice();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        const std::string frame = "_Frame" + std::to_string(i);

        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, images[i], memories[i]);
        views_[i] =
            createImageView(vkDevice, images[i], format_, VK_IMAGE_VIEW_TYPE_2D, createInfo.aspect, 0, mipLevels_);

        if (createInfo.perMipViews) {
            mipViews_[i].resize(mipLevels_, VK_NULL_HANDLE);
            for (uint32_t m = 0; m < mipLevels_; ++m) {
                mipViews_[i][m] =
                    createImageView(vkDevice, images[i], format_, VK_IMAGE_VIEW_TYPE_2D, createInfo.aspect, m);
                setDebugName(vkDevice, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)mipViews_[i][m],
                             createInfo.name + "MipView" + frame + "_Mip" + std::to_string(m));
            }
        }

        setDebugName(vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)images[i], createInfo.name + "Image" + frame);
        setDebugName(vkDevice, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)views_[i], createInfo.name + "View" + frame);
        setDebugName(vkDevice, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)memories[i], createInfo.name + "Memory" + frame);
    }
}

FrameImage::~FrameImage() {
    const VkDevice vkDevice = device.getDevice();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (VkImageView mipView : mipViews_[i]) {
            vkDestroyImageView(vkDevice, mipView, nullptr);
        }
        vkDestroyImageView(vkDevice, views_[i], nullptr);
        vkDestroyImage(vkDevice, images[i], nullptr);
        vkFreeMemory(vkDevice, memories[i], nullptr);
    }
}

} // namespace Rendering
