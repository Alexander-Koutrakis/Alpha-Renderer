#include "pipeline_layouts.hpp"
#include <stdexcept>

namespace Rendering {

VkPushConstantRange pushConstantRange(VkShaderStageFlags stages, uint32_t size, uint32_t offset) {
    return {stages, offset, size};
}

VkPipelineLayout createPipelineLayout(VkDevice device, ArrayView<VkDescriptorSetLayout> setLayouts,
                                      ArrayView<VkPushConstantRange> pushConstants) {
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    info.pSetLayouts = setLayouts.begin();
    info.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    info.pPushConstantRanges = pushConstants.begin();

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
    return layout;
}

} // namespace Rendering
