// Link seams for GPU calls that code under test references but never reaches. light_system.cpp mixes the CPU light
// math (tested) with writes into mapped buffers (not tested: they need a device). If a test ever does reach one of
// these, it fails loudly instead of corrupting memory.
#include "Rendering/Core/buffer.hpp"

#include <stdexcept>

namespace Rendering {

void Buffer::writeToBuffer(void*, VkDeviceSize, VkDeviceSize) {
    throw std::logic_error("test reached Buffer::writeToBuffer, which needs a Vulkan device");
}

} // namespace Rendering
