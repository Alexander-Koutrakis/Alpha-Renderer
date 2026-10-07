// Force-included into every translation unit of a scratch build (see scripts/descriptor_call_log.ps1, run with
// -Header scripts/pipeline_layout_call_log/pipeline_layout_log.hpp -Prefix PLOG). Wraps vkCreatePipelineLayout so a
// run prints every field of every create-info: flags, pNext, the descriptor set layouts and the push-constant ranges.
// Handles are replaced by ordinals ("layout#3" for a pipeline layout, "setlayout#5" for a descriptor set layout)
// assigned in order of first appearance, so two builds that create the same layouts in the same order print identical
// logs, and the diff of the logs proves a refactor did not change a set layout, its position, or a push-constant range.
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

namespace pipeline_layout_log {

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

inline VkResult createPipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* info,
                                     const VkAllocationCallbacks* allocator, VkPipelineLayout* layout) {
    VkResult result = ::vkCreatePipelineLayout(device, info, allocator, layout);
    std::string line = "PLOG LAYOUT " + ordinal("layout", reinterpret_cast<uintptr_t>(*layout)) +
                       " pNext=" + (info->pNext ? "set" : "null") + " flags=" + std::to_string(info->flags) +
                       " sets=" + std::to_string(info->setLayoutCount);
    for (uint32_t i = 0; i < info->setLayoutCount; ++i) {
        line += " " + ordinal("setlayout", reinterpret_cast<uintptr_t>(info->pSetLayouts[i]));
    }
    line += " pushRanges=" + std::to_string(info->pushConstantRangeCount);
    for (uint32_t i = 0; i < info->pushConstantRangeCount; ++i) {
        const VkPushConstantRange& r = info->pPushConstantRanges[i];
        line += " [stages=" + std::to_string(r.stageFlags) + " offset=" + std::to_string(r.offset) +
                " size=" + std::to_string(r.size) + "]";
    }
    std::fprintf(stderr, "%s .\n", line.c_str());
    return result;
}

} // namespace pipeline_layout_log

// Defined after the wrapper so its own call reaches the real function.
#define vkCreatePipelineLayout pipeline_layout_log::createPipelineLayout
