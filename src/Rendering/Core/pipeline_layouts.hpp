#pragma once

#include "array_view.hpp"
#include "core.hpp"

namespace Rendering {

VkPushConstantRange pushConstantRange(VkShaderStageFlags stages, uint32_t size, uint32_t offset = 0);

// Creates a pipeline layout from descriptor set layouts (set 0 first) and push-constant ranges.
// Throws std::runtime_error on failure.
VkPipelineLayout createPipelineLayout(VkDevice device, ArrayView<VkDescriptorSetLayout> setLayouts,
                                      ArrayView<VkPushConstantRange> pushConstants = {});

} // namespace Rendering
