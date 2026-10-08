#include "doctest.h"
#include "Rendering/rendering_constants.hpp"

#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

// The lighting shaders declare fixed-size arrays and index them with limits that must equal the C++ limits that size
// the buffers and descriptor sets. A mismatch is silent out-of-bounds access on the GPU, so it is checked here.

namespace {

std::map<std::string, int64_t> readGlslIntConstants(const std::string& path) {
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.is_open(), "cannot open " << path);
    std::stringstream text;
    text << file.rdbuf();

    std::map<std::string, int64_t> constants;
    const std::regex declaration(R"(const\s+int\s+(\w+)\s*=\s*(\d+)\s*;)");
    const std::string source = text.str();
    for (std::sregex_iterator it(source.begin(), source.end(), declaration), end; it != end; ++it) {
        constants[(*it)[1]] = std::stoll((*it)[2]);
    }
    return constants;
}

} // namespace

TEST_CASE("GLSL light limits equal the C++ limits") {
    const auto glsl = readGlslIntConstants(std::string(ALPHA_SHADER_DIR) + "/common/light.glsl");

    struct Pair {
        const char* glslName;
        int64_t cppValue;
    };
    const Pair pairs[] = {
        {"MAX_LIGHTS", Rendering::MAX_LIGHTS},
        {"MAX_CASCADE_COUNT", Rendering::MAX_SHADOW_CASCADE_COUNT},
        {"MAX_SHADOWCASTING_DIRECTIONAL", Rendering::MAX_DIRECTIONAL_LIGHTS},
        {"MAX_SHADOWCASTING_SPOT", Rendering::MAX_SPOT_LIGHTS},
        {"MAX_SHADOWCASTING_POINT", Rendering::MAX_POINT_LIGHTS},
        {"MAX_SHADOWCASTING_LIGHT_MATRICES", Rendering::MAX_SHADOWCASTING_LIGHT_MATRICES},
    };
    for (const Pair& pair : pairs) {
        INFO("GLSL constant " << pair.glslName);
        const auto found = glsl.find(pair.glslName);
        REQUIRE_MESSAGE(found != glsl.end(), pair.glslName << " is not declared in light.glsl");
        CHECK(found->second == pair.cppValue);
    }
}
