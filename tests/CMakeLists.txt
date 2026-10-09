# Unit tests for the CPU-side code that needs no GPU: math, octree, ECS, bounding boxes, transforms, scene lights, light system.
# They compile the real sources and use the Vulkan and GLFW headers only (no device, no window).
add_executable(AlphaTests
  main.cpp
  gpu_stubs.cpp
  aabb_test.cpp
  view_frustum_test.cpp
  octree_test.cpp
  shader_constants_test.cpp
  shader_layout_test.cpp
  ecs_test.cpp
  bounding_box_system_test.cpp
  transform_system_test.cpp
  scene_test.cpp
  light_system_test.cpp

  ${CMAKE_SOURCE_DIR}/src/Math/AABB.cpp
  ${CMAKE_SOURCE_DIR}/src/Math/view_frustum.cpp
  ${CMAKE_SOURCE_DIR}/src/ECS/ecs.cpp
  ${CMAKE_SOURCE_DIR}/src/Scene/scene.cpp
  ${CMAKE_SOURCE_DIR}/src/Systems/bounding_box_system.cpp
  ${CMAKE_SOURCE_DIR}/src/Systems/light_system.cpp
  ${CMAKE_SOURCE_DIR}/src/Systems/transform_system.cpp
)

target_include_directories(AlphaTests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/external/libraries/doctest
)

# ECS and octree headers include device.hpp, which includes ktxvulkan.h: KTX headers only, no library.
target_include_directories(AlphaTests SYSTEM PRIVATE $<TARGET_PROPERTY:KTX::ktx,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(AlphaTests PRIVATE Vulkan::Headers glfw glm::glm)

if(NOT MSVC)
  target_compile_options(AlphaTests PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  if(ALPHA_WARNINGS_AS_ERRORS)
    target_compile_options(AlphaTests PRIVATE -Werror)
  endif()
  # Link the MinGW runtime statically so the test executable starts without msys64 on PATH.
  target_link_options(AlphaTests PRIVATE -static)
endif()

# shader_constants_test.cpp reads the shared GLSL limits.
target_compile_definitions(AlphaTests PRIVATE ALPHA_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
# shader_layout_test.cpp reads the block layouts that scripts/dump_block_layouts.ps1 writes next to the compiled shaders.
target_compile_definitions(AlphaTests PRIVATE ALPHA_BLOCK_LAYOUTS="${CMAKE_SOURCE_DIR}/build/shader-check/block_layouts.txt")

add_test(NAME AlphaTests COMMAND AlphaTests)
# The shadow-budget tests trigger the engine's over-budget warning on purpose; keep it out of the test output.
set_tests_properties(AlphaTests PROPERTIES ENVIRONMENT "ALPHA_LOG=error")
