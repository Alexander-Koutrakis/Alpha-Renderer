// descriptors.hpp
#pragma once

#include "device.hpp"

// std
#include <memory>
#include <initializer_list>
#include <map>
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

// What a descriptor pool has to hold, summed from the layouts it will serve: for every layout, the number of sets that
// will be allocated from it. Each descriptor type's total is (binding count x sets) over all layouts, so the pool
// cannot drift from the layouts. Build the pool from it with DescriptorPool::Builder::fromBudget.
class DescriptorBudget {
public:
    // Reserves `setCount` sets of a layout with these bindings.
    void add(const VkDescriptorSetLayoutBinding* bindings, uint32_t bindingCount, uint32_t setCount);

    uint32_t maxSets() const { return sets; }
    // One entry per descriptor type in use, in type order; never an entry with a zero count.
    std::vector<VkDescriptorPoolSize> poolSizes() const;

private:
    uint32_t sets = 0;
    std::map<VkDescriptorType, uint32_t> descriptorCounts;
};

// Creates the layout and reserves `setCount` sets of it in `budget`, so a layout cannot exist without a stated count.
VkDescriptorSetLayout createDescriptorSetLayout(Device& device,
                                                std::initializer_list<VkDescriptorSetLayoutBinding> bindings,
                                                DescriptorBudget& budget, uint32_t setCount);

class DescriptorPool {
public:
    class Builder {
    public:
        Builder(Device& device) : device{device} {}

        // Sets the maximum set count and every pool size from the budget.
        Builder& fromBudget(const DescriptorBudget& budget);
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