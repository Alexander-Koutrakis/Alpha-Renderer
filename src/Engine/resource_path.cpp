#include "Engine/resource_path.hpp"

#include <fstream>
#include <stdexcept>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Engine {

const std::filesystem::path& executableDirectory() {
    static const std::filesystem::path directory = [] {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) {
                throw std::runtime_error("GetModuleFileNameW failed");
            }
            if (length < buffer.size()) {
                buffer.resize(length);
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
        return std::filesystem::path{buffer}.parent_path();
    }();
    return directory;
}

std::filesystem::path resourcePath(const std::string& logicalPath) {
    return executableDirectory() / logicalPath;
}

std::vector<char> readBinaryResource(const std::string& logicalPath) {
    const std::filesystem::path path = resourcePath(logicalPath);
    std::ifstream file{path, std::ios::ate | std::ios::binary};
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open resource: " + path.string());
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    return buffer;
}

} // namespace Engine
