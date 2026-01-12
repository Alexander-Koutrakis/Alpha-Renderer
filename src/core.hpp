#pragma once

// Vulkan header
#include <vulkan/vulkan.h>

// GLM configuration macros for Vulkan
#define GLM_FORCE_RADIANS
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <GLFW/glfw3.h>
// Basic GLM headers
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/common.hpp>
// Additional GLM extensions (optional)
#include <glm/gtx/quaternion.hpp>

