#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Shaders and assets are located relative to the executable, not the working directory.
// Paths in scene data and shader lists stay logical ("shaders/x.spv", "Assets/Scene/...").
namespace Engine {

// Directory that contains the running executable.
const std::filesystem::path& executableDirectory();

// Resolves a logical resource path against the executable directory.
std::filesystem::path resourcePath(const std::string& logicalPath);

// Reads a whole binary resource (SPIR-V). Throws std::runtime_error if it cannot be opened.
std::vector<char> readBinaryResource(const std::string& logicalPath);

} // namespace Engine
