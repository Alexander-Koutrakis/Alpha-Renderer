#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/frame_context.hpp"
#include "Rendering/Core/compute_pipeline.hpp"
#include "Rendering/Core/descriptors.hpp"
#include "Rendering/rendering_constants.hpp"
#include "rc_bands.hpp"
#include "Scene/scene.hpp"

#include <array>
#include <memory>
#include <vector>

namespace Rendering {

class RCGIPass {
public:
    struct CreateInfo {
        uint32_t width{0};
        uint32_t height{0};
        VkDescriptorSetLayout depthPyramidSetLayout{VK_NULL_HANDLE};
        VkFormat depthPyramidFormat{VK_FORMAT_UNDEFINED};
        VkDescriptorSetLayout rcBuildSetLayout{VK_NULL_HANDLE};
        VkDescriptorSetLayout rcResolveSetLayout{VK_NULL_HANDLE};
        VkDescriptorSetLayout skyboxSetLayout{VK_NULL_HANDLE};
    };

    RCGIPass(Device& device, const CreateInfo& createInfo);
    ~RCGIPass();

    RCGIPass(const RCGIPass&) = delete;
    RCGIPass& operator=(const RCGIPass&) = delete;

    // What the pass computes. ALPHA_RC_MODE selects it at startup: unset or "cascades" is the normal path, "reference"
    // replaces build and merge with a brute-force screen-space reference (shaders/rc_reference.comp), "off" writes no GI.
    // The reference and off modes exist to judge the cascades, see RadianceCascadeFixPlan.md.
    enum class Mode : int { Cascades = 0, Reference = 1, Off = 2 };
    static Mode modeFromEnvironment();
    // ALPHA_RC_MAX_CASCADE=k (0 to RC_CASCADE_COUNT - 1, default the last) limits the pass to cascades 0..k, and the reference
    // to the distance they reach, so the two can be compared band by band. Diagnostic only.
    static uint32_t maxCascadeFromEnvironment();

    // Entry point: sequences compute stages
    void run(FrameContext& frameContext);

    // Push-constant blocks of the compute shaders; tests/shader_layout_test.cpp checks them against the GLSL.
    struct CascadeBuildPushConstants {
        int cascadeIndex = 0;
        int probeStridePx = 0;
        int tileSize = 0;
        int depthMipCount = 0;
        int frameIndex = 0;
        float tStart = 0.0f;
        float segmentLen = 0.0f;
    };

    struct ResolvePushConstants {
        glm::mat4 prevViewProj{1.0f}; // Previous frame's view-projection matrix for reprojection
        int probeStridePx = 0;
        int tileSize = 0;
        int temporalFrame = 0; // Frame counter for jittering
        int mode = 0;          // Mode: how the resolve gets its irradiance
    };

    struct DepthPyramidPushConstants {
        float cameraNear = 0.0f;
        float cameraFar = 0.0f;
        float padding = 0.0f;
    };

private:
    // Depth pyramid stage (current)
    void createDepthPyramidPipeline();
    void buildDepthPyramid(FrameContext& frameContext);
    void setDepthPyramidBarriersBefore(FrameContext& frameContext);
    void setMipLevelBarriers(FrameContext& frameContext, uint32_t mipLevel);
    void setDepthPyramidCompletedBarriers(FrameContext& frameContext);

    // RC build/resolve stages
    void createRCBuildPipeline();
    void createRCResolvePipeline();
    void createRCMergePipeline();
    void createRCReferencePipeline();
    void traceReference(FrameContext& frameContext);
    void buildRCCascades(FrameContext& frameContext);
    void mergeRCCascades(FrameContext& frameContext);
    void resolveIndirect(FrameContext& frameContext);
    void emitComputeBarrier(VkCommandBuffer cmd) const;

    struct CascadeDispatchInfo {
        CascadeBuildPushConstants push{};
        uint32_t probeCountX{0};
        uint32_t probeCountY{0};
        uint32_t groupsX{0};
        uint32_t groupsY{0};
    };

    CascadeDispatchInfo prepareCascadeDispatch(uint32_t cascadeIndex) const;
    void dispatchCascade(VkCommandBuffer cmd, const CascadeDispatchInfo& dispatchInfo) const;

    Device& device;
    CreateInfo info;
    Mode mode{Mode::Cascades};
    uint32_t maxCascade{Rendering::RC_CASCADE_COUNT - 1u};

    // Depth pyramid compute
    VkPipelineLayout depthPyramidPipelineLayout{VK_NULL_HANDLE};
    std::unique_ptr<ComputePipeline> depthPyramidSeedPipeline;
    std::unique_ptr<ComputePipeline> depthPyramidDownsamplePipeline;
    VkSampler rcSampler{VK_NULL_HANDLE};

    // RC build/resolve compute
    VkPipelineLayout rcBuildPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout rcResolvePipelineLayout{VK_NULL_HANDLE};
    std::unique_ptr<ComputePipeline> rcBuildPipeline;
    std::unique_ptr<ComputePipeline> rcMergePipeline;
    std::unique_ptr<ComputePipeline> rcReferencePipeline;
    std::unique_ptr<ComputePipeline> rcResolvePipeline;
};

} // namespace Rendering
