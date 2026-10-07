// Force-included into every translation unit of a scratch build (see scripts/descriptor_call_log.ps1, run with
// -Header scripts/image_call_log/image_log.hpp -Prefix ILOG). Wraps vkCreateImage and vkCreateImageView so a run
// prints every field of every create-info, with Vulkan handles replaced by ordinals ("image#3", "view#7", ...)
// assigned in order of first appearance. Two builds that create the same images and views in the same order print
// identical logs, so the diff of the logs proves a refactor did not change a format, usage, extent, mip or layer
// count, view type, aspect or subresource range.
//
// Not part of the product build: never include this from src/.
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vulkan/vulkan.h>
// ktxvulkan.h has struct members named vkCreateImage*, which the macros below would break; include it first.
#if __has_include(<ktxvulkan.h>)
#include <ktxvulkan.h>
#endif

namespace image_log {

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

template <typename T> inline uint64_t raw(T handle) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
}

inline VkResult createImage(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* allocator,
                            VkImage* image) {
    VkResult result = ::vkCreateImage(device, info, allocator, image);
    std::fprintf(stderr,
                 "ILOG IMAGE %s pNext=%s flags=%u type=%d format=%d extent=%ux%ux%u mips=%u layers=%u samples=%d "
                 "tiling=%d usage=%u sharing=%d queueFamilies=%u layout=%d .\n",
                 ordinal("image", raw(*image)).c_str(), info->pNext ? "set" : "null", info->flags, info->imageType,
                 info->format, info->extent.width, info->extent.height, info->extent.depth, info->mipLevels,
                 info->arrayLayers, info->samples, info->tiling, info->usage, info->sharingMode,
                 info->queueFamilyIndexCount, info->initialLayout);
    return result;
}

inline VkResult createImageView(VkDevice device, const VkImageViewCreateInfo* info,
                                const VkAllocationCallbacks* allocator, VkImageView* view) {
    VkResult result = ::vkCreateImageView(device, info, allocator, view);
    const VkImageSubresourceRange& r = info->subresourceRange;
    std::fprintf(stderr,
                 "ILOG VIEW %s of %s pNext=%s flags=%u type=%d format=%d swizzle=%d,%d,%d,%d aspect=%u "
                 "mips=%u+%u layers=%u+%u .\n",
                 ordinal("view", raw(*view)).c_str(), ordinal("image", raw(info->image)).c_str(),
                 info->pNext ? "set" : "null", info->flags, info->viewType, info->format, info->components.r,
                 info->components.g, info->components.b, info->components.a, r.aspectMask, r.baseMipLevel,
                 r.levelCount, r.baseArrayLayer, r.layerCount);
    return result;
}

} // namespace image_log

// Defined after the wrappers so the wrappers' own calls reach the real functions.
#define vkCreateImage image_log::createImage
#define vkCreateImageView image_log::createImageView
