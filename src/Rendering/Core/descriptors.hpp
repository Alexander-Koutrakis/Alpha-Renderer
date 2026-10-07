// descriptors.hpp
#pragma once

#include "device.hpp"

// std
#include <memory>
#include <initializer_list>
#include <vector>

namespace Rendering {

// A native Vulkan layout binding with named parameters. A plain VkDescriptorSetLayoutBinding is positional
// ({binding, type, count, stages, samplers}) and count/stages are both uint32_t, so a swap would compile silently.
inline VkDescriptorSetLayoutBinding layoutBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages,
                                                  uint32_t count = 1) {
    return {binding, type, count, stages, nullptr};
}

// Creates a descriptor set layout from bindings, in the order given. Throws on failure. The caller owns the handle,
// names it and destroys it.
VkDescriptorSetLayout createDescriptorSetLayout(Device& device, const VkDescriptorSetLayoutBinding* bindings,
                                                uint32_t bindingCount);
VkDescriptorSetLayout createDescriptorSetLayout(Device& device,
                                                std::initializer_list<VkDescriptorSetLayoutBinding> bindings);

class DescriptorPool {
public:
    class Builder {
    public:
        Builder(Device& device) : device{device} {}

        Builder& addPoolSize(VkDescriptorType descriptorType, uint32_t count);
        Builder& setPoolFlags(VkDescriptorPoolCreateFlags flags);
        Builder& setMaxSets(uint32_t count);
        std::unique_ptr<DescriptorPool> build() const;

    private:
        Device& device;
        std::vector<VkDescriptorPoolSize> poolSizes{};
        uint32_t maxSets = 1000;
        VkDescriptorPoolCreateFlags poolFlags = 0;
    };

    DescriptorPool(Device& device, uint32_t maxSets, VkDescriptorPoolCreateFlags poolFlags,
                   const std::vector<VkDescriptorPoolSize>& poolSizes);
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }
    Device& device() const { return device_; }

private:
    Device& device_;
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
};

// Allocates one descriptor set with the given layout from the pool. Throws if the pool cannot provide it.
VkDescriptorSet allocateDescriptorSet(DescriptorPool& pool, VkDescriptorSetLayout layout);

// Collects descriptor writes and applies them to a set in one vkUpdateDescriptorSets call, in the order they were added.
// The infos are copied, so the writer does not depend on the lifetime of the caller's structs.
//
//   DescriptorWriter()
//       .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraBuffer->descriptorInfo())
//       .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
//       .update(device, set);
class DescriptorWriter {
public:
    DescriptorWriter& buffer(uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo& info);
    DescriptorWriter& image(uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo& info);
    // Array binding: one write covering `count` consecutive array elements starting at element 0.
    DescriptorWriter& images(uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo* infos,
                             uint32_t count);

    void update(Device& device, VkDescriptorSet set) const;

private:
    struct Entry {
        uint32_t binding = 0;
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLER;
        std::vector<VkDescriptorBufferInfo> buffers;
        std::vector<VkDescriptorImageInfo> images;
    };
    std::vector<Entry> entries;
};

} // namespace Rendering