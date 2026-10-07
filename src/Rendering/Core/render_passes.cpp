#include "render_passes.hpp"
#include <stdexcept>

namespace Rendering {

VkFramebuffer createFramebuffer(VkDevice device, VkRenderPass renderPass, ArrayView<VkImageView> attachments,
                                uint32_t width, uint32_t height, uint32_t layers) {
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = renderPass;
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.begin();
    info.width = width;
    info.height = height;
    info.layers = layers;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device, &info, nullptr, &framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
    }
    return framebuffer;
}

} // namespace Rendering
