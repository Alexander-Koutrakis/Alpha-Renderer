#include "shadow_map.hpp"
#include <stdexcept>

namespace Rendering {

ShadowMap::ShadowMap(Device& device, const ShadowMapCreateInfo& createInfo)
    : device{device}, 
      width{createInfo.width}, 
      height{createInfo.height},
      arrayLayers{createInfo.arrayLayers},
      depthFormat{createInfo.depthFormat} {
    createResources();
    createSampler();
}

ShadowMap::~ShadowMap() {
    cleanup();
}

void ShadowMap::cleanup() {
    vkDeviceWaitIdle(device.getDevice());

    // Destroy sampler
    if (shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), shadowSampler, nullptr);
        shadowSampler = VK_NULL_HANDLE;
    }

    // Per-frame resource cleanup
    for (auto view : layerViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device.getDevice(), view, nullptr);
        }
    }
    layerViews.clear();

    if (depthView != VK_NULL_HANDLE) {
        vkDestroyImageView(device.getDevice(), depthView, nullptr);
        depthView = VK_NULL_HANDLE;
    }

    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device.getDevice(), depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }

    if (depthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device.getDevice(), depthMemory, nullptr);
        depthMemory = VK_NULL_HANDLE;
    }

}

void ShadowMap::createResources() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    // Add flags for cubemap if necessary
    if (arrayLayers == 6) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    device.createImageWithInfo(
        imageInfo,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage,
        depthMemory
    );

    // Transition image to SHADER_READ_ONLY_OPTIMAL layout right after creation
    VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depthImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = arrayLayers;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    device.endSingleTimeCommands(commandBuffer);

    createImageView();
}

void ShadowMap::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow map sampler!");
    }
}

void ShadowMap::createImageView() {
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = (arrayLayers == 6) ? VK_IMAGE_VIEW_TYPE_CUBE : 
                        (arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY :VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = arrayLayers;

    if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &depthView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow map image view!");
    }

    // Create per-layer 2D views for single-layer rendering when arrayLayers > 1
    layerViews.resize(arrayLayers);
    for (uint32_t layer = 0; layer < arrayLayers; ++layer) {
        VkImageViewCreateInfo layerViewInfo{};
        layerViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        layerViewInfo.image = depthImage;
        layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerViewInfo.format = depthFormat;
        layerViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        layerViewInfo.subresourceRange.baseMipLevel = 0;
        layerViewInfo.subresourceRange.levelCount = 1;
        layerViewInfo.subresourceRange.baseArrayLayer = layer;
        layerViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.getDevice(), &layerViewInfo, nullptr, &layerViews[layer]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow map layer image view!");
        }
    }
}

} // namespace Rendering