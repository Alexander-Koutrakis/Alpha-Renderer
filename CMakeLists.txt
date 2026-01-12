cmake_minimum_required(VERSION 3.15)
project(AlphaRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------
# Compiler warnings
# ---------------------------------------
if(MSVC)
  add_compile_definitions(_WINDOWS _WIN32)
else()
  add_compile_options(-Wall -Wextra -g)
endif()

# ---------------------------------------
# Vulkan
# ---------------------------------------
find_package(Vulkan REQUIRED)

# ---------------------------------------
# GLFW (MinGW / manual install)
# ---------------------------------------
find_library(GLFW_LIBRARY NAMES glfw3
  PATHS
    C:/msys64/mingw64/lib
  REQUIRED
)

include_directories(C:/msys64/mingw64/include)

# ---------------------------------------
# KTX
# ---------------------------------------
set(KTX_ROOT "C:/Program Files/KTX-Software")
set(KTX_INCLUDE_DIR "${KTX_ROOT}/include")
set(KTX_LIBRARY "${KTX_ROOT}/lib/ktx.lib")

# ---------------------------------------
# GLM (header-only)
# ---------------------------------------
set(GLM_INCLUDE_DIR "C:/msys64/mingw64/include")

# ---------------------------------------
# ImGui (vendor library)
# ---------------------------------------
add_library(imgui STATIC
  "external/libraries/imgui/imgui.cpp"
  "external/libraries/imgui/imgui_draw.cpp"
  "external/libraries/imgui/imgui_tables.cpp"
  "external/libraries/imgui/imgui_widgets.cpp"
  "external/libraries/imgui/imgui_demo.cpp"
  "external/libraries/imgui/imgui_impl_glfw.cpp"
  "external/libraries/imgui/imgui_impl_vulkan.cpp"
)

target_include_directories(imgui PUBLIC
  "external/libraries/imgui"
)

target_link_libraries(imgui PUBLIC
  Vulkan::Vulkan
  ${GLFW_LIBRARY}
)

# ---------------------------------------
# Engine / Application
# ---------------------------------------
add_executable(main
  "src/main.cpp"

  # Engine
  "src/Engine/alpha_engine.cpp"

  # Rendering Core
  "src/Rendering/renderer.cpp"
  "src/Rendering/Core/window.cpp"
  "src/Rendering/Core/device.cpp"
  "src/Rendering/Core/swapchain.cpp"
  "src/Rendering/Core/imgui_manager.cpp"
  "src/Rendering/Core/pipeline.cpp"
  "src/Rendering/Core/descriptors.cpp"
  "src/Rendering/Core/compute_pipeline.cpp"
  "src/Rendering/Core/buffer.cpp"

  # Rendering Resources
  "src/Rendering/Resources/rendering_resources.cpp"
  "src/Rendering/Resources/gbuffer.cpp"
  "src/Rendering/Resources/material.cpp"
  "src/Rendering/Resources/texture.cpp"
  "src/Rendering/Resources/mesh.cpp"

  # Render Passes
  "src/Rendering/RenderPasses/Shadowmapping/shadow_pass.cpp"
  "src/Rendering/RenderPasses/Shadowmapping/shadow_map.cpp"
  "src/Rendering/RenderPasses/Transparency/transparency_pass.cpp"
  "src/Rendering/RenderPasses/SMAA/smaa_weight_pass.cpp"
  "src/Rendering/RenderPasses/SMAA/smaa_edge_pass.cpp"
  "src/Rendering/RenderPasses/SMAA/smaa_blend_pass.cpp"
  "src/Rendering/RenderPasses/Radiance Cascades/rc_gi_pass.cpp"
  "src/Rendering/RenderPasses/Geometry/geometry_pass.cpp"
  "src/Rendering/RenderPasses/General/skybox_pass.cpp"
  "src/Rendering/RenderPasses/Direct Lighting/light_pass.cpp"
  "src/Rendering/RenderPasses/Composition/composition_pass.cpp"
  "src/Rendering/RenderPasses/Color Correction/color_correction_pass.cpp"

  # Systems
  "src/Systems/camera_system.cpp"
  "src/Systems/camera_culling.cpp"
  "src/Systems/keyboard_movement_system.cpp"
  "src/Systems/light_system.cpp"
  "src/Systems/transform_system.cpp"
  "src/Systems/bounding_box_system.cpp"

  # Resources
  "src/Resources/resource_manager.cpp"
  "src/Resources/scene_loader.cpp"
  "src/Resources/deserialized_scene.cpp"

  # Scene
  "src/Scene/scene.cpp"

  # Math
  "src/Math/view_frustum.cpp"
  "src/Math/AABB.cpp"

  # ECS
  "src/ECS/ecs.cpp"

  # External
  "external/libraries/base64.cpp"
)

target_include_directories(main PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GLM_INCLUDE_DIR}
  ${KTX_INCLUDE_DIR}
)

target_link_libraries(main PRIVATE
  Vulkan::Vulkan
  ${GLFW_LIBRARY}
  ${KTX_LIBRARY}
  imgui
)

# ---------------------------------------
# Shader compilation
# ---------------------------------------
set(GLSLC "$ENV{VULKAN_SDK}/Bin/glslc.exe")

file(GLOB_RECURSE SHADERS
  shaders/*.vert
  shaders/*.frag
  shaders/*.comp
)

set(SPV_OUTPUTS "")
foreach(SHADER ${SHADERS})
  get_filename_component(FILE ${SHADER} NAME)
  set(SPV "${CMAKE_BINARY_DIR}/shaders/${FILE}.spv")

  add_custom_command(
    OUTPUT ${SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${GLSLC} ${SHADER} -o ${SPV}
    DEPENDS ${SHADER}
  )

  list(APPEND SPV_OUTPUTS ${SPV})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPV_OUTPUTS})
add_dependencies(main shaders)

# Copy shaders to executable directory (build/shaders/) - runs every build
add_custom_target(copy_shaders ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:main>/shaders
  COMMENT "Copying shaders to executable directory"
  DEPENDS shaders
)

# Copy Assets directory to build/Assets/ - runs every build
# Using custom targets ensures they run even when main.exe doesn't need rebuilding
add_custom_target(copy_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Assets
    ${CMAKE_BINARY_DIR}/Assets
  COMMENT "Copying Assets directory to build folder"
  DEPENDS shaders
)

# Set working directory to build directory for running the executable
# This ensures relative paths like "shaders/" and "Assets/" resolve correctly
# when running from the build directory
set_property(TARGET main PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
# (They're already there from compilation, but this ensures they stay)