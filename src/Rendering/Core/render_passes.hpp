#pragma once

#include "array_view.hpp"
#include "core.hpp"

namespace Rendering {

// Creates a framebuffer over the given attachment views, in the order the render pass declares them.
// Throws std::runtime_error on failure.
VkFramebuffer createFramebuffer(VkDevice device, VkRenderPass renderPass, ArrayView<VkImageView> attachments,
                                uint32_t width, uint32_t height, uint32_t layers = 1);

// Single-sampled attachment whose stencil aspect is ignored (DONT_CARE), which is what every pass in the renderer wants.
VkAttachmentDescription attachmentDescription(VkFormat format, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp,
                                              VkImageLayout initialLayout, VkImageLayout finalLayout);

// Creates a render pass with one subpass. The attachments, the subpass (and the references it points to) and the
// dependencies only need to outlive the call. Throws std::runtime_error on failure.
VkRenderPass createRenderPass(VkDevice device, ArrayView<VkAttachmentDescription> attachments,
                              const VkSubpassDescription& subpass, ArrayView<VkSubpassDependency> dependencies);

} // namespace Rendering
