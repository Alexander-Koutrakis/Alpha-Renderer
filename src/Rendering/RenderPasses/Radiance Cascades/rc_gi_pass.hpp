#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/frame_context.hpp"
#include "Rendering/Core/compute_pipeline.hpp"
#include "Rendering/Core/descriptors.hpp"
#include "Rendering/rendering_constants.hpp"
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

    // Entry point: sequences compute stages
    void run(FrameContext& frameContext);

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
    void buildRCCascades(FrameContext& frameContext);
    void mergeRCCascades(FrameContext& frameContext);
    void resolveIndirect(FrameContext& frameContext);
    void emitComputeBarrier(VkCommandBuffer cmd) const;

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
    };
    struct CascadeDispatchInfo {
        CascadeBuildPushConstants push{};
        uint32_t probeCountX{0};
        uint32_t probeCountY{0};
        uint32_t groupsX{0};
        uint32_t groupsY{0};
    };

    struct DepthPyramidPushConstants {
        float cameraNear = 0.0f;
        float cameraFar = 0.0f;
        float padding = 0.0f;
    };

    struct CascadeBand {
        float start = 0.0f;
        float length = 0.0f;
    };
    CascadeDispatchInfo prepareCascadeDispatch(uint32_t cascadeIndex, const CascadeBand& band) const;
    void dispatchCascade(VkCommandBuffer cmd, const CascadeDispatchInfo& dispatchInfo) const;
    void computeCascadeBands();

    Device& device;
    CreateInfo info;

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
    std::unique_ptr<ComputePipeline> rcResolvePipeline;
    std::array<CascadeBand, Rendering::RC_CASCADE_COUNT> cascadeBands{};
};

} // namespace Rendering
