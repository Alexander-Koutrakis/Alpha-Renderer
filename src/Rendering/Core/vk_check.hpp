#pragma once

#include "core.hpp"
#include <stdexcept>
#include <string>
#include <vulkan/vk_enum_string_helper.h>

namespace Rendering {

// Throws std::runtime_error naming the failed call, its VkResult and the source location. Use through VK_CHECK.
// Not for calls where a non-success result is an expected outcome (acquire/present return OUT_OF_DATE on resize).
inline void vkCheck(VkResult result, const char* expression, const char* file, int line) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(string_VkResult(result)) + " from " + expression + " at " + file + ":" +
                                 std::to_string(line));
    }
}

} // namespace Rendering

#define VK_CHECK(expression) ::Rendering::vkCheck((expression), #expression, __FILE__, __LINE__)
