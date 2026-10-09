#include "debug_names.hpp"

namespace Rendering {

void setDebugName(VkDevice device, VkObjectType objectType, uint64_t handle, const std::string& name) {
    VkDebugUtilsObjectNameInfoEXT nameInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = handle;
    nameInfo.pObjectName = name.c_str();

    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
    if (func != nullptr) {
        func(device, &nameInfo);
    }
}

} // namespace Rendering
