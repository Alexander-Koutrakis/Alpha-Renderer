#include "doctest.h"
#include "Rendering/RenderPasses/Shadowmapping/shadow_pass.hpp"
#include "Rendering/RenderPasses/radiance_cascades/rc_gi_pass.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/Resources/material.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Every uniform block and push-constant block in the shaders is filled from a C++ struct. Vulkan checks neither side
// against the other, and glslc and the validation layers accept any mismatch, so a missing field or a different order
// reads shifted bytes on the GPU. This test compares the block layouts that scripts/dump_block_layouts.ps1 reads out
// of the compiled SPIR-V with the C++ structs.
//
// A GLSL block may be a prefix of its C++ struct (the vertex stages declare CameraUbo without clipPlanes), but never
// the other way round, and an array in the block must be as long as the C++ array. Scalar types are not compared, only
// offsets, strides and sizes (an `int` in GLSL is a `uint32_t` in the C++ Light struct on purpose). Shader storage
// buffers are not covered.

namespace {

using namespace Rendering;

struct Field {
    std::string name;
    size_t offset;
    size_t arrayStride; // 0 if not an array
    size_t arrayLength; // 0 if not an array
};

struct Layout {
    size_t size;
    std::vector<Field> fields;
};

#define FIELD(T, f) Field{#f, offsetof(T, f), 0, 0}
#define ARRAY_FIELD(T, f)                                                                                              \
    Field {                                                                                                            \
        #f, offsetof(T, f), sizeof(T::f[0]), sizeof(T::f) / sizeof(T::f[0])                                            \
    }

const Layout cameraUbo{sizeof(CameraUbo),
                       {FIELD(CameraUbo, view), FIELD(CameraUbo, proj), FIELD(CameraUbo, viewProj),
                        FIELD(CameraUbo, cameraPosition), FIELD(CameraUbo, clipPlanes)}};

const Layout sceneLightingUbo{sizeof(SceneLightingUbo),
                              {FIELD(SceneLightingUbo, viewMatrix), FIELD(SceneLightingUbo, projectionMatrix),
                               FIELD(SceneLightingUbo, cameraPosition), FIELD(SceneLightingUbo, ambientIntensity),
                               FIELD(SceneLightingUbo, reflectionIntensity)}};

const Layout light{sizeof(Light),
                   {FIELD(Light, positionAndData), FIELD(Light, colorAndIntensity), FIELD(Light, directionAndRange),
                    FIELD(Light, attenuationParams), FIELD(Light, lightType), FIELD(Light, lightMatrixOffset),
                    FIELD(Light, shadowmapIndex), FIELD(Light, isCastingShadow), FIELD(Light, shadowStrength)}};

const Layout unifiedLightBuffer{sizeof(UnifiedLightBuffer),
                                {ARRAY_FIELD(UnifiedLightBuffer, lights), FIELD(UnifiedLightBuffer, lightCount)}};

const Layout shadowcastingLightMatrices{sizeof(ShadowcastingLightMatrices),
                                        {ARRAY_FIELD(ShadowcastingLightMatrices, shadowcastingLightMatrices)}};

const Layout cascadesBuffer{sizeof(DirectionalLightCascadesBuffer),
                            {ARRAY_FIELD(DirectionalLightCascadesBuffer, cascadeSplits)}};

const Layout materialUbo{sizeof(MaterialUbo),
                         {FIELD(MaterialUbo, albedoColor), FIELD(MaterialUbo, metallic), FIELD(MaterialUbo, smoothness),
                          FIELD(MaterialUbo, ao), FIELD(MaterialUbo, alphaCutoff), FIELD(MaterialUbo, isMasked),
                          FIELD(MaterialUbo, isEmissive), FIELD(MaterialUbo, hasAlbedoMap),
                          FIELD(MaterialUbo, hasMetallicSmoothnessMap), FIELD(MaterialUbo, hasNormalMap),
                          FIELD(MaterialUbo, hasOcclusionMap), FIELD(MaterialUbo, normalStrength)}};

using CascadeBuildPC = RCGIPass::CascadeBuildPushConstants;
const Layout cascadeBuildPush{sizeof(CascadeBuildPC),
                              {FIELD(CascadeBuildPC, cascadeIndex), FIELD(CascadeBuildPC, probeStridePx),
                               FIELD(CascadeBuildPC, tileSize), FIELD(CascadeBuildPC, depthMipCount),
                               FIELD(CascadeBuildPC, frameIndex), FIELD(CascadeBuildPC, tStart),
                               FIELD(CascadeBuildPC, segmentLen)}};

using ResolvePC = RCGIPass::ResolvePushConstants;
const Layout resolvePush{sizeof(ResolvePC),
                         {FIELD(ResolvePC, prevViewProj), FIELD(ResolvePC, probeStridePx), FIELD(ResolvePC, tileSize),
                          FIELD(ResolvePC, temporalFrame)}};

using DepthPC = RCGIPass::DepthPyramidPushConstants;
const Layout depthPush{sizeof(DepthPC), {FIELD(DepthPC, cameraNear), FIELD(DepthPC, cameraFar)}};

using ShadowPC = ShadowPass::InstancedPushConstants;
const Layout shadowPush{sizeof(ShadowPC),
                        {FIELD(ShadowPC, lightPosRange), FIELD(ShadowPC, lightMatrixIndex),
                         FIELD(ShadowPC, modelMatrixOffset), FIELD(ShadowPC, lightType)}};

// The geometry and transparency passes push a bare `uint32_t` (the instance offset) without a struct:
// vkCmdPushConstants(..., 0, sizeof(uint32_t), ...) in geometry_pass.cpp and transparency_pass.cpp.
const Layout instanceOffsetPush{sizeof(uint32_t), {Field{"instanceOffset", 0, 0, 0}}};

struct Binding {
    const char* shader;
    const char* block;
    const Layout* layout;
    // GLSL member name -> C++ member name, for the members the two sides name differently.
    std::map<std::string, std::string> rename;
};

// Every block the shaders use, with the C++ struct that fills it. A block missing here fails the test, and so does an
// entry here that no shader has any more.
const std::vector<Binding> bindings = {
    {"direct_light.frag", "EnvironmentLightingUbo", &sceneLightingUbo, {}},
    {"direct_light.frag", "LightUbo", &unifiedLightBuffer, {}},
    {"direct_light.frag", "Light", &light, {}},
    {"direct_light.frag", "ShadowcastingLightMatrices", &shadowcastingLightMatrices, {}},
    {"direct_light.frag", "DirectionalLightCascadeSplits", &cascadesBuffer, {}},
    {"geometry.frag", "MaterialUbo", &materialUbo, {}},
    {"geometry.vert", "CameraUbo", &cameraUbo, {{"projection", "proj"}, {"viewProjection", "viewProj"}}},
    {"geometry.vert", "pushConstants", &instanceOffsetPush, {}},
    {"rc_build_cascade.comp", "CameraUBO", &cameraUbo, {{"camPos", "cameraPosition"}}},
    {"rc_build_cascade.comp", "pc", &cascadeBuildPush, {}},
    {"rc_depth_copy.comp", "pc", &depthPush, {}},
    {"rc_merge.comp", "pc", &cascadeBuildPush, {}},
    {"rc_resolve_indirect.comp", "CameraUBO", &cameraUbo, {{"camPos", "cameraPosition"}}},
    {"rc_resolve_indirect.comp", "pc", &resolvePush, {}},
    {"shadowmap.frag", "MaterialUbo", &materialUbo, {}},
    {"shadowmap.frag",
     "push",
     &shadowPush,
     {{"matrixIndex", "lightMatrixIndex"}, {"instanceOffset", "modelMatrixOffset"}}},
    {"shadowmap.vert",
     "ShadowUBO",
     &shadowcastingLightMatrices,
     {{"lightSpaceMatrices", "shadowcastingLightMatrices"}}},
    {"shadowmap.vert", "push", &shadowPush, {}},
    {"skybox.vert", "CameraUbo", &cameraUbo, {{"projection", "proj"}, {"viewProjection", "viewProj"}}},
    {"transparency.frag", "SceneLightingUbo", &sceneLightingUbo, {}},
    {"transparency.frag", "DirectionalLightCascadeSplits", &cascadesBuffer, {}},
    {"transparency.frag", "ShadowcastingLightMatrices", &shadowcastingLightMatrices, {}},
    {"transparency.frag", "MaterialUbo", &materialUbo, {}},
    {"transparency.frag", "CameraUBO", &cameraUbo, {{"projection", "proj"}, {"viewProjection", "viewProj"}}},
    {"transparency.frag", "LightUbo", &unifiedLightBuffer, {}},
    {"transparency.frag", "Light", &light, {}},
    {"transparency.vert", "CameraUbo", &cameraUbo, {{"projection", "proj"}, {"viewProjection", "viewProj"}}},
    {"transparency.vert", "pushConstants", &instanceOffsetPush, {}},
};

struct GlslMember {
    std::string name;
    size_t offset;
    size_t arrayStride;
    size_t arrayLength;
};

struct GlslBlock {
    std::string shader;
    std::string name;
    std::string kind; // ubo, push or struct
    size_t size;
    std::vector<GlslMember> members;
};

std::vector<GlslBlock> readGlslBlocks(const std::string& path) {
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.is_open(), "cannot open " << path << " (run `just shaders`, which writes it)");
    std::vector<GlslBlock> blocks;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream words(line);
        std::string tag;
        words >> tag;
        if (tag == "block") {
            GlslBlock block;
            words >> block.shader >> block.name >> block.kind >> block.size;
            REQUIRE_MESSAGE(!words.fail(), "bad block line: " << line);
            blocks.push_back(block);
        } else if (tag == "member") {
            GlslMember member;
            words >> member.name >> member.offset >> member.arrayStride >> member.arrayLength;
            REQUIRE_MESSAGE(!words.fail(), "bad member line: " << line);
            REQUIRE_MESSAGE(!blocks.empty(), "member line before any block line: " << line);
            blocks.back().members.push_back(member);
        } else {
            FAIL("unknown line in " << path << ": " << line);
        }
    }
    return blocks;
}

} // namespace

TEST_CASE("shader uniform and push-constant blocks match their C++ structs") {
    const auto blocks = readGlslBlocks(ALPHA_BLOCK_LAYOUTS);
    REQUIRE(!blocks.empty());

    std::set<std::pair<std::string, std::string>> seen;
    for (const GlslBlock& block : blocks) {
        INFO("shader " << block.shader << ", block " << block.name);
        seen.insert({block.shader, block.name});

        const Binding* binding = nullptr;
        for (const Binding& candidate : bindings) {
            if (block.shader == candidate.shader && block.name == candidate.block)
                binding = &candidate;
        }
        REQUIRE_MESSAGE(binding != nullptr, "block has no C++ struct registered in shader_layout_test.cpp");
        const Layout& layout = *binding->layout;

        if (block.kind == "struct") {
            CHECK_MESSAGE(layout.size == block.size,
                          "array element size: C++ " << layout.size << ", GLSL " << block.size);
        } else {
            CHECK_MESSAGE(layout.size >= block.size,
                          "C++ struct is " << layout.size << " bytes, the block needs " << block.size);
        }

        REQUIRE_MESSAGE(block.members.size() <= layout.fields.size(), "block has " << block.members.size()
                                                                                   << " members, the C++ struct only "
                                                                                   << layout.fields.size());
        for (size_t i = 0; i < block.members.size(); ++i) {
            const GlslMember& glsl = block.members[i];
            const auto renamed = binding->rename.find(glsl.name);
            const std::string& cppName = renamed != binding->rename.end() ? renamed->second : glsl.name;
            const Field& cpp = layout.fields[i];
            INFO("member " << i << ": GLSL '" << glsl.name << "', C++ '" << cpp.name << "'");
            CHECK(cpp.name == cppName);
            CHECK_MESSAGE(cpp.offset == glsl.offset, "offset: C++ " << cpp.offset << ", GLSL " << glsl.offset);
            CHECK_MESSAGE(cpp.arrayStride == glsl.arrayStride,
                          "array stride: C++ " << cpp.arrayStride << ", GLSL " << glsl.arrayStride);
            CHECK_MESSAGE(cpp.arrayLength == glsl.arrayLength,
                          "array length: C++ " << cpp.arrayLength << ", GLSL " << glsl.arrayLength);
        }
    }

    for (const Binding& binding : bindings) {
        CHECK_MESSAGE(seen.count({binding.shader, binding.block}) == 1, "registered block "
                                                                            << binding.shader << " " << binding.block
                                                                            << " is not in any shader any more");
    }
}
