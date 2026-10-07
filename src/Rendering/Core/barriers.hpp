#pragma once

#include "core.hpp"
#include <array>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace Rendering {

// Non-owning view over a contiguous run of T. Lets barrier calls accept a braced list, a std::array or a
// std::vector without copying. A view built from a braced list is only valid until the end of the full
// expression, which is exactly how the barrier functions below use it.
template <typename T> class ArrayView {
public:
    ArrayView() = default;
    // GCC's -Winit-list-lifetime flags keeping a braced list's pointer. Here the view is only a function
    // parameter, so the list outlives it (the same contract C++26 gives std::span).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"
#endif
    ArrayView(std::initializer_list<T> list) : first(list.begin()), count(list.size()) {}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    ArrayView(const std::vector<T>& v) : first(v.data()), count(v.size()) {}
    template <std::size_t N> ArrayView(const std::array<T, N>& a) : first(a.data()), count(N) {}
    ArrayView(const T& single) : first(&single), count(1) {}

    const T* begin() const { return first; }
    const T* end() const { return first + count; }
    std::size_t size() const { return count; }

private:
    const T* first = nullptr;
    std::size_t count = 0;
};

// Image barrier described by what it means rather than by the 14 fields of VkImageMemoryBarrier.
// Field order is chosen so the common case reads as {image, aspect, from, to, srcAccess, dstAccess}.
struct ImageBarrierDesc {
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags srcAccess = 0;
    VkAccessFlags dstAccess = 0;
    uint32_t baseMipLevel = 0;
    uint32_t levelCount = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount = 1;

    // First use of a freshly created image: contents are undefined, it will be sampled by a shader.
    static ImageBarrierDesc undefinedToShaderRead(VkImage image, VkImageAspectFlags aspect, uint32_t layerCount = 1,
                                                  uint32_t levelCount = 1) {
        return {image,
                aspect,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                0,
                VK_ACCESS_SHADER_READ_BIT,
                0,
                levelCount,
                0,
                layerCount};
    }

    // First use of a freshly created storage image that compute shaders will write.
    static ImageBarrierDesc undefinedToGeneralShaderWrite(VkImage image) {
        return {image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                0,     VK_ACCESS_SHADER_WRITE_BIT};
    }
};

struct BufferBarrierDesc {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkAccessFlags srcAccess = 0;
    VkAccessFlags dstAccess = 0;
    VkDeviceSize offset = 0;
    VkDeviceSize size = VK_WHOLE_SIZE;

    // CPU-written data (UBOs, instance buffers) that a shader reads later in the frame.
    static BufferBarrierDesc hostWriteToShaderRead(VkBuffer buffer) {
        return {buffer, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT};
    }
};

struct MemoryBarrierDesc {
    VkAccessFlags srcAccess = 0;
    VkAccessFlags dstAccess = 0;
};

// One mip level of one array layer (colour aspect) changing layout, as used when generating mip chains.
inline ImageBarrierDesc mipLevelBarrier(VkImage image, uint32_t mipLevel, uint32_t arrayLayer, VkImageLayout oldLayout,
                                        VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    return {image, VK_IMAGE_ASPECT_COLOR_BIT, oldLayout, newLayout, srcAccess, dstAccess, mipLevel, 1, arrayLayer, 1};
}

// Conversions to the Vulkan structs (sType set, queue families ignored, everything else zeroed).
VkImageMemoryBarrier toVk(const ImageBarrierDesc& desc);
VkBufferMemoryBarrier toVk(const BufferBarrierDesc& desc);
VkMemoryBarrier toVk(const MemoryBarrierDesc& desc);

// Records one vkCmdPipelineBarrier. Any of the three lists may be empty.
void cmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                        ArrayView<MemoryBarrierDesc> memoryBarriers, ArrayView<BufferBarrierDesc> bufferBarriers,
                        ArrayView<ImageBarrierDesc> imageBarriers, VkDependencyFlags dependencyFlags = 0);

inline void cmdImageBarriers(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage, ArrayView<ImageBarrierDesc> imageBarriers,
                             VkDependencyFlags dependencyFlags = 0) {
    cmdPipelineBarrier(commandBuffer, srcStage, dstStage, {}, {}, imageBarriers, dependencyFlags);
}

inline void cmdBufferBarriers(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStage,
                              VkPipelineStageFlags dstStage, ArrayView<BufferBarrierDesc> bufferBarriers,
                              VkDependencyFlags dependencyFlags = 0) {
    cmdPipelineBarrier(commandBuffer, srcStage, dstStage, {}, bufferBarriers, {}, dependencyFlags);
}

inline void cmdMemoryBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage, const MemoryBarrierDesc& barrier,
                             VkDependencyFlags dependencyFlags = 0) {
    cmdPipelineBarrier(commandBuffer, srcStage, dstStage, barrier, {}, {}, dependencyFlags);
}

} // namespace Rendering
