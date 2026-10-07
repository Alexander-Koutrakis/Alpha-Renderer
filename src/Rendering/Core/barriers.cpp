#include "barriers.hpp"
#include <stdexcept>
#include <string>

namespace Rendering {

namespace {
// Largest barrier batch recorded in one call anywhere in the renderer; grow if a call needs more.
constexpr std::size_t MAX_BARRIERS_PER_CALL = 16;

template <typename Vk, typename Desc>
std::array<Vk, MAX_BARRIERS_PER_CALL> convert(ArrayView<Desc> descs, const char* what) {
    if (descs.size() > MAX_BARRIERS_PER_CALL) {
        throw std::length_error(std::string("too many ") + what + " in one vkCmdPipelineBarrier");
    }
    std::array<Vk, MAX_BARRIERS_PER_CALL> out{};
    std::size_t i = 0;
    for (const Desc& d : descs) {
        out[i++] = toVk(d);
    }
    return out;
}
} // namespace

VkImageMemoryBarrier toVk(const ImageBarrierDesc& desc) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = desc.srcAccess;
    barrier.dstAccessMask = desc.dstAccess;
    barrier.oldLayout = desc.oldLayout;
    barrier.newLayout = desc.newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = desc.image;
    barrier.subresourceRange.aspectMask = desc.aspect;
    barrier.subresourceRange.baseMipLevel = desc.baseMipLevel;
    barrier.subresourceRange.levelCount = desc.levelCount;
    barrier.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
    barrier.subresourceRange.layerCount = desc.layerCount;
    return barrier;
}

VkBufferMemoryBarrier toVk(const BufferBarrierDesc& desc) {
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = desc.srcAccess;
    barrier.dstAccessMask = desc.dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = desc.buffer;
    barrier.offset = desc.offset;
    barrier.size = desc.size;
    return barrier;
}

VkMemoryBarrier toVk(const MemoryBarrierDesc& desc) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = desc.srcAccess;
    barrier.dstAccessMask = desc.dstAccess;
    return barrier;
}

void cmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                        ArrayView<MemoryBarrierDesc> memoryBarriers, ArrayView<BufferBarrierDesc> bufferBarriers,
                        ArrayView<ImageBarrierDesc> imageBarriers, VkDependencyFlags dependencyFlags) {
    const auto memory = convert<VkMemoryBarrier>(memoryBarriers, "memory barriers");
    const auto buffers = convert<VkBufferMemoryBarrier>(bufferBarriers, "buffer barriers");
    const auto images = convert<VkImageMemoryBarrier>(imageBarriers, "image barriers");

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, dependencyFlags,
                         static_cast<uint32_t>(memoryBarriers.size()), memory.data(),
                         static_cast<uint32_t>(bufferBarriers.size()), buffers.data(),
                         static_cast<uint32_t>(imageBarriers.size()), images.data());
}

} // namespace Rendering
