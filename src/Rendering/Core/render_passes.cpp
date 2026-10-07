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

VkAttachmentDescription attachmentDescription(VkFormat format, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp,
                                              VkImageLayout initialLayout, VkImageLayout finalLayout) {
    VkAttachmentDescription description{};
    description.format = format;
    description.samples = VK_SAMPLE_COUNT_1_BIT;
    description.loadOp = loadOp;
    description.storeOp = storeOp;
    description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    description.initialLayout = initialLayout;
    description.finalLayout = finalLayout;
    return description;
}

VkRenderPass createRenderPass(VkDevice device, ArrayView<VkAttachmentDescription> attachments,
                              const VkSubpassDescription& subpass, ArrayView<VkSubpassDependency> dependencies) {
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.begin();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    info.pDependencies = dependencies.begin();

    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &info, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
    return renderPass;
}

} // namespace Rendering
