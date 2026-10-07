#pragma once

#include "core.hpp"

namespace Rendering {

// Creates a sampler with the same filter for magnification and minification, the same address mode on all three axes,
// no comparison, normalized coordinates and minLod 0, which is the shape every sampler in the renderer has.
// maxAnisotropy > 0 turns anisotropic filtering on with that limit; 0 leaves it off. Throws std::runtime_error on failure.
VkSampler createSampler(VkDevice device, VkFilter filter, VkSamplerMipmapMode mipmapMode,
                        VkSamplerAddressMode addressMode, VkBorderColor borderColor, float maxLod,
                        float maxAnisotropy = 0.0f, float mipLodBias = 0.0f);

} // namespace Rendering
