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

class DescriptorWriter {
public:
    DescriptorWriter(VkDescriptorSetLayout layout, DescriptorPool& pool);

    DescriptorWriter& writeBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo,
                                  VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    DescriptorWriter& writeImage(uint32_t binding, VkDescriptorImageInfo* imageInfo);
    DescriptorWriter& writeInputAttachment(uint32_t binding, VkDescriptorImageInfo* imageInfo);
    bool build(VkDescriptorSet& set);
    void overwrite(VkDescriptorSet& set);

private:
    VkDescriptorSetLayout setLayout;
    DescriptorPool& pool;
    std::vector<VkWriteDescriptorSet> writes;
};

} // namespace Rendering