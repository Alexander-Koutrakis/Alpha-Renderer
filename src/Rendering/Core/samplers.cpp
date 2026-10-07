#include "samplers.hpp"
#include <stdexcept>

namespace Rendering {

VkSampler createSampler(VkDevice device, VkFilter filter, VkSamplerMipmapMode mipmapMode,
                        VkSamplerAddressMode addressMode, VkBorderColor borderColor, float maxLod, float maxAnisotropy,
                        float mipLodBias) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = mipmapMode;
    info.addressModeU = addressMode;
    info.addressModeV = addressMode;
    info.addressModeW = addressMode;
    info.mipLodBias = mipLodBias;
    info.anisotropyEnable = maxAnisotropy > 0.0f ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = maxAnisotropy > 0.0f ? maxAnisotropy : 1.0f;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.minLod = 0.0f;
    info.maxLod = maxLod;
    info.borderColor = borderColor;
    info.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create sampler!");
    }
    return sampler;
}

} // namespace Rendering
