#include "shadow_map.hpp"
#include "Rendering/Core/barriers.hpp"
#include "Rendering/Core/images.hpp"
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
    // Cube-compatible when the array holds six faces
    VkImageCreateInfo imageInfo = imageCreateInfo2D(
        width, height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1,
        arrayLayers, arrayLayers == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0);

    device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthMemory);

    // Transition image to SHADER_READ_ONLY_OPTIMAL layout right after creation
    VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

    cmdImageBarriers(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     ImageBarrierDesc::undefinedToShaderRead(depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, arrayLayers));

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
    depthView = Rendering::createImageView(device.getDevice(), depthImage, depthFormat,
                                           (arrayLayers == 6)  ? VK_IMAGE_VIEW_TYPE_CUBE
                                           : (arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                               : VK_IMAGE_VIEW_TYPE_2D,
                                           VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, arrayLayers);

    // Create per-layer 2D views for single-layer rendering when arrayLayers > 1
    layerViews.resize(arrayLayers);
    for (uint32_t layer = 0; layer < arrayLayers; ++layer) {
        layerViews[layer] = Rendering::createImageView(device.getDevice(), depthImage, depthFormat,
                                                       VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, layer);
    }
}

} // namespace Rendering