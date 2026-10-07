#pragma once

#include "array_view.hpp"
#include "core.hpp"

namespace Rendering {

// Creates a framebuffer over the given attachment views, in the order the render pass declares them.
// Throws std::runtime_error on failure.
VkFramebuffer createFramebuffer(VkDevice device, VkRenderPass renderPass, ArrayView<VkImageView> attachments,
                                uint32_t width, uint32_t height, uint32_t layers = 1);

} // namespace Rendering
