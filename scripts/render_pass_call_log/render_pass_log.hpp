// Force-included into every translation unit of a scratch build (see scripts/descriptor_call_log.ps1, run with
// -Header scripts/render_pass_call_log/render_pass_log.hpp -Prefix RLOG). Wraps vkCreateRenderPass and
// vkCreateFramebuffer so a run prints every attachment, subpass reference and dependency of every render pass, and the
// render pass, attachment views and extent of every framebuffer. Handles are replaced by ordinals ("pass#2",
// "view#7", ...) assigned in order of first appearance, so two builds that create the same render passes and
// framebuffers in the same order print identical logs.
//
// Stencil load/store ops are only printed for formats that have a stencil aspect: the spec ignores them otherwise,
// and the old code left them at 0 (LOAD/STORE) in the geometry pass and set DONT_CARE elsewhere.
//
// Not part of the product build: never include this from src/.
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vulkan/vulkan.h>
#if __has_include(<ktxvulkan.h>)
#include <ktxvulkan.h>
#endif

namespace render_pass_log {

// stderr is block-buffered when it is redirected to a file; see descriptor_log.hpp.
inline const int stderrUnbuffered = (std::setvbuf(stderr, nullptr, _IONBF, 0), 0);

inline std::string ordinal(const char* kind, uint64_t handle) {
    static std::map<std::string, std::map<uint64_t, int>> ids;
    if (handle == 0) {
        return std::string(kind) + "#null";
    }
    auto& perKind = ids[kind];
    auto it = perKind.find(handle);
    if (it == perKind.end()) {
        it = perKind.emplace(handle, static_cast<int>(perKind.size())).first;
    }
    return std::string(kind) + "#" + std::to_string(it->second);
}

inline bool hasStencil(VkFormat f) {
    return f == VK_FORMAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT || f == VK_FORMAT_D24_UNORM_S8_UINT ||
           f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

inline std::string refs(const char* name, const VkAttachmentReference* r, uint32_t count) {
    std::string s = std::string(" ") + name + "=" + std::to_string(count);
    for (uint32_t i = 0; r && i < count; ++i) {
        s += " (" + std::to_string(r[i].attachment) + "," + std::to_string(r[i].layout) + ")";
    }
    return s;
}

inline VkResult createRenderPass(VkDevice device, const VkRenderPassCreateInfo* info,
                                 const VkAllocationCallbacks* allocator, VkRenderPass* pass) {
    VkResult result = ::vkCreateRenderPass(device, info, allocator, pass);
    std::string head = "RLOG PASS " + ordinal("pass", reinterpret_cast<uintptr_t>(*pass)) +
                       " pNext=" + (info->pNext ? "set" : "null") + " flags=" + std::to_string(info->flags);
    std::fprintf(stderr, "%s attachments=%u subpasses=%u dependencies=%u .\n", head.c_str(), info->attachmentCount,
                 info->subpassCount, info->dependencyCount);
    for (uint32_t i = 0; i < info->attachmentCount; ++i) {
        const VkAttachmentDescription& a = info->pAttachments[i];
        std::string stencil = hasStencil(a.format) ? " stencilOps=" + std::to_string(a.stencilLoadOp) + "," +
                                                         std::to_string(a.stencilStoreOp)
                                                   : " stencilOps=-";
        std::fprintf(stderr, "RLOG   ATTACHMENT %u flags=%u format=%d samples=%d ops=%d,%d%s layouts=%d->%d .\n", i,
                     a.flags, a.format, a.samples, a.loadOp, a.storeOp, stencil.c_str(), a.initialLayout,
                     a.finalLayout);
    }
    for (uint32_t i = 0; i < info->subpassCount; ++i) {
        const VkSubpassDescription& s = info->pSubpasses[i];
        std::string line = "RLOG   SUBPASS " + std::to_string(i) + " flags=" + std::to_string(s.flags) +
                           " bind=" + std::to_string(s.pipelineBindPoint);
        line += refs("input", s.pInputAttachments, s.inputAttachmentCount);
        line += refs("color", s.pColorAttachments, s.colorAttachmentCount);
        line += refs("resolve", s.pResolveAttachments, s.pResolveAttachments ? s.colorAttachmentCount : 0);
        line += refs("depth", s.pDepthStencilAttachment, s.pDepthStencilAttachment ? 1 : 0);
        line += " preserve=" + std::to_string(s.preserveAttachmentCount);
        for (uint32_t p = 0; p < s.preserveAttachmentCount; ++p) {
            line += " " + std::to_string(s.pPreserveAttachments[p]);
        }
        std::fprintf(stderr, "%s .\n", line.c_str());
    }
    for (uint32_t i = 0; i < info->dependencyCount; ++i) {
        const VkSubpassDependency& d = info->pDependencies[i];
        std::fprintf(stderr, "RLOG   DEPENDENCY %u %u->%u stage=%u->%u access=%u->%u flags=%u .\n", i, d.srcSubpass,
                     d.dstSubpass, d.srcStageMask, d.dstStageMask, d.srcAccessMask, d.dstAccessMask,
                     d.dependencyFlags);
    }
    return result;
}

inline VkResult createFramebuffer(VkDevice device, const VkFramebufferCreateInfo* info,
                                  const VkAllocationCallbacks* allocator, VkFramebuffer* framebuffer) {
    VkResult result = ::vkCreateFramebuffer(device, info, allocator, framebuffer);
    std::string line = "RLOG FRAMEBUFFER " + ordinal("fb", reinterpret_cast<uintptr_t>(*framebuffer)) +
                       " pNext=" + (info->pNext ? "set" : "null") + " flags=" + std::to_string(info->flags) +
                       " pass=" + ordinal("pass", reinterpret_cast<uintptr_t>(info->renderPass)) +
                       " extent=" + std::to_string(info->width) + "x" + std::to_string(info->height) +
                       " layers=" + std::to_string(info->layers) + " views=" + std::to_string(info->attachmentCount);
    for (uint32_t i = 0; i < info->attachmentCount; ++i) {
        line += " " + ordinal("view", reinterpret_cast<uintptr_t>(info->pAttachments[i]));
    }
    std::fprintf(stderr, "%s .\n", line.c_str());
    return result;
}

} // namespace render_pass_log

// Defined after the wrappers so their own calls reach the real functions.
#define vkCreateRenderPass render_pass_log::createRenderPass
#define vkCreateFramebuffer render_pass_log::createFramebuffer
