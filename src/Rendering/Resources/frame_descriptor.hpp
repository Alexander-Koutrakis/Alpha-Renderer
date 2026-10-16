#pragma once

#include "Rendering/Core/descriptors.hpp"
#include "Rendering/Core/device.hpp"
#include "Rendering/rendering_constants.hpp"
#include <array>
#include <string>
#include <vector>

namespace Rendering {

// One descriptor set layout and every set allocated from it: `frameCount` frames of `setsPerFrame` sets each.
// Creating it reserves those sets in the pool budget; allocate() fills them once the pool exists. Destroys the layout;
// the sets are freed with the pool.
class FrameDescriptor {
public:
    struct CreateInfo {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        uint32_t frameCount{MAX_FRAMES_IN_FLIGHT}; // 1 for a set that is not per frame
        uint32_t setsPerFrame{1};
        // Debug names: <name>DescriptorSetLayout, <name>DescriptorSet_Frame<i>, plus _<index> when setsPerFrame > 1
        std::string name;
    };

    FrameDescriptor(Device& device, const CreateInfo& createInfo, DescriptorBudget& budget);
    ~FrameDescriptor();

    FrameDescriptor(const FrameDescriptor&) = delete;
    FrameDescriptor& operator=(const FrameDescriptor&) = delete;

    // Allocates every set, frame by frame, index by index.
    void allocate(DescriptorPool& pool);

    VkDescriptorSetLayout layout() const { return layout_; }
    VkDescriptorSet set(size_t frame, uint32_t index = 0) const { return sets_[frame][index]; }
    const std::vector<VkDescriptorSet>& sets(size_t frame) const { return sets_[frame]; }

private:
    Device& device;
    VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
    uint32_t frameCount;
    uint32_t setsPerFrame;
    std::string name;
    std::array<std::vector<VkDescriptorSet>, MAX_FRAMES_IN_FLIGHT> sets_{};
};

} // namespace Rendering
