#include "frame_descriptor.hpp"
#include "Rendering/Core/debug_names.hpp"
#include <stdexcept>

namespace Rendering {

FrameDescriptor::FrameDescriptor(Device& device, const CreateInfo& createInfo, DescriptorBudget& budget)
    : device{device}, frameCount{createInfo.frameCount}, setsPerFrame{createInfo.setsPerFrame}, name{createInfo.name} {
    if (frameCount == 0 || frameCount > MAX_FRAMES_IN_FLIGHT || setsPerFrame == 0) {
        throw std::runtime_error("FrameDescriptor " + name + ": frameCount must be 1.." +
                                 std::to_string(MAX_FRAMES_IN_FLIGHT) + " and setsPerFrame at least 1");
    }
    const auto bindingCount = static_cast<uint32_t>(createInfo.bindings.size());
    budget.add(createInfo.bindings.data(), bindingCount, frameCount * setsPerFrame);
    layout_ = createDescriptorSetLayout(device, createInfo.bindings.data(), bindingCount);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)layout_,
                 name + "DescriptorSetLayout");
}

FrameDescriptor::~FrameDescriptor() {
    vkDestroyDescriptorSetLayout(device.getDevice(), layout_, nullptr);
}

void FrameDescriptor::allocate(DescriptorPool& pool) {
    for (uint32_t frame = 0; frame < frameCount; frame++) {
        sets_[frame].resize(setsPerFrame);
        for (uint32_t index = 0; index < setsPerFrame; index++) {
            sets_[frame][index] = allocateDescriptorSet(pool, layout_);
            std::string setName = name + "DescriptorSet_Frame" + std::to_string(frame);
            if (setsPerFrame > 1) {
                setName += "_" + std::to_string(index);
            }
            setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)sets_[frame][index], setName);
        }
    }
}

} // namespace Rendering
