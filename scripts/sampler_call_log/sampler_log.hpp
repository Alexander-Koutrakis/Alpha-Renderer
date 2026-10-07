// Force-included into every translation unit of a scratch build (see scripts/descriptor_call_log.ps1, run with
// -Header scripts/sampler_call_log/sampler_log.hpp -Prefix SLOG). Wraps vkCreateSampler so a run prints every field of
// every sampler create-info, with the sampler handle replaced by an ordinal ("sampler#3") assigned in order of first
// appearance. Two builds that create the same samplers in the same order print identical logs, so the diff of the
// logs proves a refactor did not change a filter, address mode, LOD range, anisotropy or border colour.
//
// compareOp is only printed when compareEnable is set: the spec ignores it otherwise, and the old code left it at 0
// in some places and set ALWAYS in others.
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

namespace sampler_log {

// stderr is block-buffered when it is redirected to a file; see descriptor_log.hpp.
inline const int stderrUnbuffered = (std::setvbuf(stderr, nullptr, _IONBF, 0), 0);

inline std::string ordinal(uint64_t handle) {
    static std::map<uint64_t, int> ids;
    if (handle == 0) {
        return "sampler#null";
    }
    auto it = ids.find(handle);
    if (it == ids.end()) {
        it = ids.emplace(handle, static_cast<int>(ids.size())).first;
    }
    return "sampler#" + std::to_string(it->second);
}

inline VkResult createSampler(VkDevice device, const VkSamplerCreateInfo* info, const VkAllocationCallbacks* allocator,
                              VkSampler* sampler) {
    VkResult result = ::vkCreateSampler(device, info, allocator, sampler);
    std::fprintf(stderr,
                 "SLOG SAMPLER %s pNext=%s flags=%u mag=%d min=%d mipmap=%d address=%d,%d,%d lodBias=%.9g "
                 "anisotropy=%u max=%.9g compare=%u op=%d lod=%.9g..%.9g border=%d unnormalized=%u .\n",
                 ordinal(reinterpret_cast<uintptr_t>(*sampler)).c_str(), info->pNext ? "set" : "null", info->flags,
                 info->magFilter, info->minFilter, info->mipmapMode, info->addressModeU, info->addressModeV,
                 info->addressModeW, static_cast<double>(info->mipLodBias), info->anisotropyEnable,
                 static_cast<double>(info->maxAnisotropy), info->compareEnable,
                 info->compareEnable ? static_cast<int>(info->compareOp) : -1, static_cast<double>(info->minLod),
                 static_cast<double>(info->maxLod), info->borderColor, info->unnormalizedCoordinates);
    return result;
}

} // namespace sampler_log

// Defined after the wrapper so its own call reaches the real function.
#define vkCreateSampler sampler_log::createSampler
