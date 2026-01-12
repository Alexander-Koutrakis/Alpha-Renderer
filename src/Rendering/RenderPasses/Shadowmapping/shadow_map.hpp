#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/swapchain.hpp"
#include "Rendering/rendering_constants.hpp"
#include <array>
#include <memory>
#include <vector>

namespace Rendering {

class ShadowMap {
public:

    struct ShadowMapCreateInfo {
        uint32_t width;
        uint32_t height;
        uint32_t arrayLayers;                
        VkFormat depthFormat;
    };

    ShadowMap(Device& device, const ShadowMapCreateInfo& createInfo);
    ~ShadowMap();

   
    VkImageView getImageView() const { return depthView; }
    VkImageView getLayerImageView(uint32_t layer) const { return layerViews[layer]; }
    VkSampler getSampler() const { return shadowSampler; }
    VkImage getImage() const { return depthImage; }

protected:
    void cleanup();
    void createResources();
    void createSampler();
    void createImageView();
    Device& device;
    uint32_t width;
    uint32_t height;
    uint32_t arrayLayers;
    VkFormat depthFormat;
    VkSampler shadowSampler{VK_NULL_HANDLE};

    VkImage depthImage{};
    VkDeviceMemory depthMemory{};
    VkImageView depthView{};
    std::vector<VkImageView> layerViews{};
};



}