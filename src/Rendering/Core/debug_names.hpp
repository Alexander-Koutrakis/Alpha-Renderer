#pragma once

#include "core.hpp"
#include <string>

namespace Rendering {

// Names a Vulkan object for validation messages and graphics debuggers. Does nothing when the debug utils extension
// is not loaded.
void setDebugName(VkDevice device, VkObjectType objectType, uint64_t handle, const std::string& name);

} // namespace Rendering
