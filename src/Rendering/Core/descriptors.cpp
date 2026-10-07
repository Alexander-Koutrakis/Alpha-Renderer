// descriptors.cpp
#include "descriptors.hpp"

// std
#include <stdexcept>
#include <string>

namespace Rendering {

// *************** Descriptor Set Layout *********************

VkDescriptorSetLayout createDescriptorSetLayout(Device& device, const VkDescriptorSetLayoutBinding* bindings,
                                                uint32_t bindingCount) {
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = bindingCount;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult result = vkCreateDescriptorSetLayout(device.getDevice(), &layoutInfo, nullptr, &layout);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout (VkResult " + std::to_string(result) + ")");
    }
    return layout;
}

VkDescriptorSetLayout createDescriptorSetLayout(Device& device,
                                                std::initializer_list<VkDescriptorSetLayoutBinding> bindings) {
    return createDescriptorSetLayout(device, bindings.begin(), static_cast<uint32_t>(bindings.size()));
}

// *************** Descriptor Pool Builder *********************

DescriptorPool::Builder& DescriptorPool::Builder::addPoolSize(VkDescriptorType descriptorType, uint32_t count) {
    poolSizes.push_back({descriptorType, count});
    return *this;
}

DescriptorPool::Builder& DescriptorPool::Builder::setPoolFlags(VkDescriptorPoolCreateFlags flags) {
    poolFlags = flags;
    return *this;
}

DescriptorPool::Builder& DescriptorPool::Builder::setMaxSets(uint32_t count) {
    maxSets = count;
    return *this;
}

std::unique_ptr<DescriptorPool> DescriptorPool::Builder::build() const {
    return std::make_unique<DescriptorPool>(device, maxSets, poolFlags, poolSizes);
}

// *************** Descriptor Pool *********************

DescriptorPool::DescriptorPool(Device& device, uint32_t maxSets, VkDescriptorPoolCreateFlags poolFlags,
                               const std::vector<VkDescriptorPoolSize>& poolSizes)
    : device_{device} {
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = maxSets;
    descriptorPoolInfo.flags = poolFlags;

    if (vkCreateDescriptorPool(device.getDevice(), &descriptorPoolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

DescriptorPool::~DescriptorPool() {
    vkDestroyDescriptorPool(device_.getDevice(), descriptorPool, nullptr);
}

// *************** Descriptor Set Allocation and Writer *********************

VkDescriptorSet allocateDescriptorSet(DescriptorPool& pool, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = pool.getDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult result = vkAllocateDescriptorSets(pool.device().getDevice(), &allocInfo, &set);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor set (VkResult " + std::to_string(result) +
                                 "); the descriptor pool may be exhausted");
    }
    return set;
}

DescriptorWriter& DescriptorWriter::buffer(uint32_t binding, VkDescriptorType type,
                                           const VkDescriptorBufferInfo& info) {
    Entry entry;
    entry.binding = binding;
    entry.type = type;
    entry.buffers.push_back(info);
    entries.push_back(std::move(entry));
    return *this;
}

DescriptorWriter& DescriptorWriter::image(uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo& info) {
    return images(binding, type, &info, 1);
}

DescriptorWriter& DescriptorWriter::images(uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo* infos,
                                           uint32_t count) {
    Entry entry;
    entry.binding = binding;
    entry.type = type;
    entry.images.assign(infos, infos + count);
    entries.push_back(std::move(entry));
    return *this;
}

void DescriptorWriter::update(Device& device, VkDescriptorSet set) const {
    // The write structs point into the entries' own storage, which no longer changes at this point.
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(entries.size());
    for (const Entry& entry : entries) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = entry.binding;
        write.descriptorType = entry.type;
        if (!entry.buffers.empty()) {
            write.descriptorCount = static_cast<uint32_t>(entry.buffers.size());
            write.pBufferInfo = entry.buffers.data();
        } else {
            write.descriptorCount = static_cast<uint32_t>(entry.images.size());
            write.pImageInfo = entry.images.data();
        }
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(device.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

} // namespace Rendering