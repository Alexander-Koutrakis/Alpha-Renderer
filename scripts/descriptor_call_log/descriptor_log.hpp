// Force-included into every translation unit of a scratch build (see scripts/descriptor_call_log.ps1).
// Wraps the descriptor entry points so a run prints the *structure* of every descriptor call, with Vulkan
// handles replaced by ordinals ("layout#3", "set#12", "view#7", ...) assigned in order of first appearance.
// Two builds that create and write the same descriptors in the same order print identical logs, so the diff of the
// logs proves a refactor did not change a binding, a type, a stage mask, a bound resource or the call order.
//
// Not part of the product build: never include this from src/.
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vulkan/vulkan.h>

namespace descriptor_log {

// stderr is block-buffered when it is redirected to a file, so a process that is killed after startup would leave a
// half-written last line in the log. Make it unbuffered once, at static-initialization time.
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

inline VkResult createSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* info,
                                const VkAllocationCallbacks* allocator, VkDescriptorSetLayout* layout) {
    VkResult result = ::vkCreateDescriptorSetLayout(device, info, allocator, layout);
    std::string line = "DSLOG LAYOUT " + ordinal("layout", raw(*layout)) + " flags=" + std::to_string(info->flags) +
                       " pNext=" + (info->pNext ? "set" : "null") + " bindings=" + std::to_string(info->bindingCount);
    for (uint32_t i = 0; i < info->bindingCount; ++i) {
        const VkDescriptorSetLayoutBinding& b = info->pBindings[i];
        line += " [b" + std::to_string(b.binding) + " type=" + std::to_string(b.descriptorType) +
                " count=" + std::to_string(b.descriptorCount) + " stages=" + std::to_string(b.stageFlags) +
                " imm=" + (b.pImmutableSamplers ? "set" : "null") + "]";
    }
    std::fprintf(stderr, "%s\n", line.c_str());
    return result;
}

inline VkResult allocateSets(VkDevice device, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets) {
    VkResult result = ::vkAllocateDescriptorSets(device, info, sets);
    for (uint32_t i = 0; i < info->descriptorSetCount; ++i) {
        std::fprintf(stderr, "DSLOG ALLOC %s from %s\n", ordinal("set", raw(sets[i])).c_str(),
                     ordinal("layout", raw(info->pSetLayouts[i])).c_str());
    }
    return result;
}

inline void updateSets(VkDevice device, uint32_t writeCount, const VkWriteDescriptorSet* writes, uint32_t copyCount,
                       const VkCopyDescriptorSet* copies) {
    for (uint32_t w = 0; w < writeCount; ++w) {
        const VkWriteDescriptorSet& write = writes[w];
        std::string line = "DSLOG WRITE " + ordinal("set", raw(write.dstSet)) + " b" +
                           std::to_string(write.dstBinding) + " elem=" + std::to_string(write.dstArrayElement) +
                           " type=" + std::to_string(write.descriptorType) +
                           " count=" + std::to_string(write.descriptorCount);
        for (uint32_t i = 0; i < write.descriptorCount; ++i) {
            if (write.pImageInfo) {
                const VkDescriptorImageInfo& img = write.pImageInfo[i];
                line += " {sampler=" + ordinal("sampler", raw(img.sampler)) +
                        " view=" + ordinal("view", raw(img.imageView)) + " layout=" + std::to_string(img.imageLayout) +
                        "}";
            } else if (write.pBufferInfo) {
                const VkDescriptorBufferInfo& buf = write.pBufferInfo[i];
                line += " {buffer=" + ordinal("buffer", raw(buf.buffer)) + " offset=" + std::to_string(buf.offset) +
                        " range=" + std::to_string(buf.range) + "}";
            }
        }
        std::fprintf(stderr, "%s\n", line.c_str());
    }
    for (uint32_t c = 0; c < copyCount; ++c) {
        std::fprintf(stderr, "DSLOG COPY (not expected in this renderer)\n");
    }
    ::vkUpdateDescriptorSets(device, writeCount, writes, copyCount, copies);
}

} // namespace descriptor_log

// Defined after the wrappers so the wrappers' own calls reach the real functions.
#define vkCreateDescriptorSetLayout descriptor_log::createSetLayout
#define vkAllocateDescriptorSets descriptor_log::allocateSets
#define vkUpdateDescriptorSets descriptor_log::updateSets
