#include "rc_gi_pass.hpp"
#include "Rendering/Core/barriers.hpp"
#include "Rendering/Core/pipeline_layouts.hpp"
#include "Rendering/Core/samplers.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Rendering {
constexpr uint32_t RC_BUILD_GROUP_SIZE_X = 8u;
constexpr uint32_t RC_BUILD_GROUP_SIZE_Y = 8u;
RCGIPass::Mode RCGIPass::modeFromEnvironment() {
    const char* value = std::getenv("ALPHA_RC_MODE");
    if (value == nullptr || *value == '\0' || std::strcmp(value, "cascades") == 0) {
        return Mode::Cascades;
    }
    if (std::strcmp(value, "reference") == 0) {
        return Mode::Reference;
    }
    if (std::strcmp(value, "off") == 0) {
        return Mode::Off;
    }
    throw std::runtime_error(std::string("ALPHA_RC_MODE must be cascades, reference or off, got: ") + value);
}

uint32_t RCGIPass::maxCascadeFromEnvironment() {
    const char* value = std::getenv("ALPHA_RC_MAX_CASCADE");
    if (value == nullptr || *value == '\0') {
        return RC_CASCADE_COUNT - 1u;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (*end != '\0' || parsed < 0 || parsed >= static_cast<long>(RC_CASCADE_COUNT)) {
        throw std::runtime_error(std::string("ALPHA_RC_MAX_CASCADE must be 0 to ") +
                                 std::to_string(RC_CASCADE_COUNT - 1u) + ", got: " + value);
    }
    return static_cast<uint32_t>(parsed);
}

RCGIPass::RCGIPass(Device& device, const CreateInfo& createInfo)
    : device(device), info(createInfo), mode(modeFromEnvironment()), maxCascade(maxCascadeFromEnvironment()) {
    // Create a dedicated sampler for compute sampling
    // texelFetch ignores filter, but NEAREST is explicit
    rcSampler =
        createSampler(device.getDevice(), VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, VK_LOD_CLAMP_NONE);

    createDepthPyramidPipeline();
    createRCBuildPipeline();
    createRCMergePipeline();
    createRCResolvePipeline();
    if (mode == Mode::Reference) {
        createRCReferencePipeline();
    }
}

RCGIPass::~RCGIPass() {
    if (depthPyramidSeedPipeline) {
        depthPyramidSeedPipeline.reset();
    }
    if (depthPyramidDownsamplePipeline) {
        depthPyramidDownsamplePipeline.reset();
    }
    if (depthPyramidPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), depthPyramidPipelineLayout, nullptr);
        depthPyramidPipelineLayout = VK_NULL_HANDLE;
    }
    if (rcSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), rcSampler, nullptr);
        rcSampler = VK_NULL_HANDLE;
    }
    if (rcBuildPipeline) {
        rcBuildPipeline.reset();
    }
    if (rcMergePipeline) {
        rcMergePipeline.reset();
    }
    if (rcResolvePipeline) {
        rcResolvePipeline.reset();
    }
    if (rcReferencePipeline) {
        rcReferencePipeline.reset();
    }
    if (rcBuildPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), rcBuildPipelineLayout, nullptr);
        rcBuildPipelineLayout = VK_NULL_HANDLE;
    }
    if (rcResolvePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.getDevice(), rcResolvePipelineLayout, nullptr);
        rcResolvePipelineLayout = VK_NULL_HANDLE;
    }
}

void RCGIPass::createDepthPyramidPipeline() {
    // Pipeline layout with push constants (used by downsample; seed ignores them)
    depthPyramidPipelineLayout = createPipelineLayout(
        device.getDevice(), info.depthPyramidSetLayout,
        pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, static_cast<uint32_t>(sizeof(DepthPyramidPushConstants))));

    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = depthPyramidPipelineLayout;
    depthPyramidSeedPipeline = std::make_unique<ComputePipeline>(device, "shaders/rc_depth_copy.comp.spv", cfg);
    depthPyramidDownsamplePipeline =
        std::make_unique<ComputePipeline>(device, "shaders/rc_depth_downsample.comp.spv", cfg);
}

void RCGIPass::createRCBuildPipeline() {
    // Push constants: cascadeIndex, probeStridePx, tileSize, depthMipCount, frameIndex, tStart, segmentLen
    rcBuildPipelineLayout = createPipelineLayout(
        device.getDevice(), info.rcBuildSetLayout,
        pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, static_cast<uint32_t>(sizeof(CascadeBuildPushConstants))));

    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = rcBuildPipelineLayout;
    rcBuildPipeline = std::make_unique<ComputePipeline>(device, "shaders/rc_build_cascade.comp.spv", cfg);
}

void RCGIPass::createRCMergePipeline() {
    if (rcBuildPipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = rcBuildPipelineLayout;
    rcMergePipeline = std::make_unique<ComputePipeline>(device, "shaders/rc_merge.comp.spv", cfg);
}

void RCGIPass::createRCReferencePipeline() {
    // Same descriptor set and push constants as the cascade build: it reads the same G-buffer, pyramid and incident light.
    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = rcBuildPipelineLayout;
    rcReferencePipeline = std::make_unique<ComputePipeline>(device, "shaders/rc_reference.comp.spv", cfg);
}

void RCGIPass::createRCResolvePipeline() {
    // Push constants: cascadeIndex,probeStridePx,tileSize,giIntensity

    std::array<VkDescriptorSetLayout, 2> setLayouts = {info.rcResolveSetLayout, info.skyboxSetLayout};

    rcResolvePipelineLayout = createPipelineLayout(
        device.getDevice(), setLayouts,
        pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, static_cast<uint32_t>(sizeof(ResolvePushConstants))));

    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = rcResolvePipelineLayout;
    rcResolvePipeline = std::make_unique<ComputePipeline>(device, "shaders/rc_resolve_indirect.comp.spv", cfg);
}

void RCGIPass::buildRCCascades(FrameContext& frameContext) {
    VkCommandBuffer cmd = frameContext.commandBuffer;
    if (!rcBuildPipeline) {
        return;
    }

    // Bind pipeline and descriptor set once
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcBuildPipeline->getPipeline());
    VkDescriptorSet ds = frameContext.rcBuildDescriptorSet;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcBuildPipelineLayout, 0, 1, &ds, 0, nullptr);

    // Compute probe-relative distance bands so each cascade integrates a specific ray interval
    const uint32_t depthMipLevels = frameContext.depthPyramidMipLevels;
    if (depthMipLevels == 0u) {
        return;
    }

    const int frameIndexMod = static_cast<int>(frameContext.temporalFrameIndex % 1000u);
    for (uint32_t cascade = 0; cascade <= maxCascade; ++cascade) {
        CascadeDispatchInfo dispatchInfo = prepareCascadeDispatch(cascade);
        dispatchInfo.push.depthMipCount = static_cast<int>(depthMipLevels);
        dispatchInfo.push.frameIndex = frameIndexMod;
        dispatchCascade(cmd, dispatchInfo);
    }

    emitComputeBarrier(frameContext.commandBuffer);
}

void RCGIPass::mergeRCCascades(FrameContext& frameContext) {
    VkCommandBuffer cmd = frameContext.commandBuffer;
    if (!rcMergePipeline) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcMergePipeline->getPipeline());
    VkDescriptorSet ds = frameContext.rcBuildDescriptorSet;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcBuildPipelineLayout, 0, 1, &ds, 0, nullptr);

    for (int cascade = static_cast<int>(maxCascade) - 1; cascade >= 0; --cascade) {
        CascadeDispatchInfo dispatchInfo = prepareCascadeDispatch(static_cast<uint32_t>(cascade));
        dispatchInfo.push.depthMipCount = 0;
        dispatchInfo.push.frameIndex = static_cast<int>(frameContext.temporalFrameIndex % 1000u);
        if (dispatchInfo.groupsX == 0u || dispatchInfo.groupsY == 0u) {
            continue;
        }

        vkCmdPushConstants(cmd, rcBuildPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           static_cast<uint32_t>(sizeof(CascadeBuildPushConstants)), &dispatchInfo.push);

        rcMergePipeline->dispatch(cmd, dispatchInfo.groupsX, dispatchInfo.groupsY, 1);

        emitComputeBarrier(cmd);
    }
}

RCGIPass::CascadeDispatchInfo RCGIPass::prepareCascadeDispatch(uint32_t cascadeIndex) const {
    const RcBand band = rcCascadeBand(cascadeIndex);
    CascadeDispatchInfo dispatch{};
    dispatch.push.cascadeIndex = static_cast<int>(cascadeIndex);
    dispatch.push.probeStridePx = std::max(1, static_cast<int>(Rendering::RC_PROBE_STRIDE0_PX) << cascadeIndex);
    dispatch.push.tileSize = std::max(1, static_cast<int>(Rendering::RC_BASE_TILE_SIZE) << cascadeIndex);
    dispatch.push.frameIndex = 0;
    dispatch.push.tStart = std::max(0.0f, band.start);

    // The band is traced exactly: intervals partition the ray, so L(0..2) = L(0..1) + beta(0..1) * L(1..2) holds.
    dispatch.push.segmentLen = band.length;

    if (info.width == 0 || info.height == 0) {
        return dispatch;
    }

    const uint32_t stride = static_cast<uint32_t>(dispatch.push.probeStridePx);
    if (stride == 0u) {
        return dispatch;
    }

    dispatch.probeCountX = (info.width + stride - 1u) / stride;
    dispatch.probeCountY = (info.height + stride - 1u) / stride;

    // Flatten tileSize into the dispatch so each invocation corresponds to a single
    // atlas texel (direction sample) instead of an entire probe tile.
    const uint32_t tileSize = static_cast<uint32_t>(dispatch.push.tileSize);
    const uint32_t pixelsX = dispatch.probeCountX * tileSize;
    const uint32_t pixelsY = dispatch.probeCountY * tileSize;

    dispatch.groupsX = (pixelsX + RC_BUILD_GROUP_SIZE_X - 1u) / RC_BUILD_GROUP_SIZE_X;
    dispatch.groupsY = (pixelsY + RC_BUILD_GROUP_SIZE_Y - 1u) / RC_BUILD_GROUP_SIZE_Y;
    return dispatch;
}

void RCGIPass::dispatchCascade(VkCommandBuffer cmd, const CascadeDispatchInfo& dispatchInfo) const {
    if (!rcBuildPipeline) {
        return;
    }
    if (dispatchInfo.groupsX == 0u || dispatchInfo.groupsY == 0u) {
        return;
    }
    if (dispatchInfo.push.segmentLen <= 0.0f) {
        return;
    }

    vkCmdPushConstants(cmd, rcBuildPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       static_cast<uint32_t>(sizeof(CascadeBuildPushConstants)), &dispatchInfo.push);

    rcBuildPipeline->dispatch(cmd, dispatchInfo.groupsX, dispatchInfo.groupsY, 1);
}

void RCGIPass::resolveIndirect(FrameContext& frameContext) {
    VkCommandBuffer cmd = frameContext.commandBuffer;
    if (!rcResolvePipeline) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcResolvePipeline->getPipeline());

    // Bind the resolve descriptor set
    // Assumes frameContext has rcResolveDescriptorSet matching the layout
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcResolvePipelineLayout, 0, 1,
                            &frameContext.rcResolveDescriptorSet, 0, nullptr);

    // Bind skybox descriptor set (Set 1)
    if (frameContext.skyboxDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcResolvePipelineLayout, 1, 1,
                                &frameContext.skyboxDescriptorSet, 0, nullptr);
    }

    // Push constants: base values + temporal data
    ResolvePushConstants pc{};
    pc.probeStridePx = static_cast<int>(Rendering::RC_PROBE_STRIDE0_PX);
    pc.tileSize = static_cast<int>(Rendering::RC_BASE_TILE_SIZE);
    pc.temporalFrame = static_cast<int>(frameContext.temporalFrameIndex);
    pc.prevViewProj = frameContext.prevCameraData.viewProjectionMatrix;
    pc.mode = static_cast<int>(mode);

    vkCmdPushConstants(cmd, rcResolvePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ResolvePushConstants), &pc);

    // Dispatch over screen size (8x8 group size)
    const uint32_t groupSizeX = 8;
    const uint32_t groupSizeY = 8;
    uint32_t groupsX = (info.width + groupSizeX - 1) / groupSizeX;
    uint32_t groupsY = (info.height + groupSizeY - 1) / groupSizeY;

    rcResolvePipeline->dispatch(cmd, groupsX, groupsY, 1);
}

void RCGIPass::traceReference(FrameContext& frameContext) {
    VkCommandBuffer cmd = frameContext.commandBuffer;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcReferencePipeline->getPipeline());
    VkDescriptorSet ds = frameContext.rcBuildDescriptorSet;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcBuildPipelineLayout, 0, 1, &ds, 0, nullptr);

    CascadeBuildPushConstants push{};
    push.frameIndex = static_cast<int>(frameContext.temporalFrameIndex % 1000u);
    push.segmentLen = rcCascadeReach(maxCascade); // the reference's maximum ray distance
    vkCmdPushConstants(cmd, rcBuildPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       static_cast<uint32_t>(sizeof(CascadeBuildPushConstants)), &push);

    rcReferencePipeline->dispatch(cmd, (info.width + RC_BUILD_GROUP_SIZE_X - 1u) / RC_BUILD_GROUP_SIZE_X,
                                  (info.height + RC_BUILD_GROUP_SIZE_Y - 1u) / RC_BUILD_GROUP_SIZE_Y, 1);
    emitComputeBarrier(cmd);
}

void RCGIPass::setDepthPyramidBarriersBefore(FrameContext& frameContext) {
    // Make scene depth visible to compute sampling, and move pyramid mip 0 to GENERAL for writing.
    // The previous writer of mip 0 was the depth attachment; be explicit about that dependency.
    const std::array<ImageBarrierDesc, 2> barriers{{
        {frameContext.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
         VK_ACCESS_SHADER_READ_BIT},
        mipLevelBarrier(frameContext.depthPyramidImage, 0, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT),
    }};
    cmdImageBarriers(frameContext.commandBuffer,
                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, barriers);
}

void RCGIPass::setMipLevelBarriers(FrameContext& frameContext, uint32_t mipLevel) {
    const std::array<ImageBarrierDesc, 2> barriers{{
        // previous mip: written by the last dispatch, now sampled
        mipLevelBarrier(frameContext.depthPyramidImage, mipLevel - 1, 0, VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT),
        // destination mip: conservative starting state, becomes writable
        mipLevelBarrier(frameContext.depthPyramidImage, mipLevel, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT),
    }};
    cmdImageBarriers(frameContext.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, barriers);
}

// Finalize: transition last mip to READ_ONLY for external sampling
void RCGIPass::setDepthPyramidCompletedBarriers(FrameContext& frameContext) {
    const uint32_t lastMip = frameContext.depthPyramidMipLevels - 1;
    cmdImageBarriers(frameContext.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     mipLevelBarrier(frameContext.depthPyramidImage, lastMip, 0, VK_IMAGE_LAYOUT_GENERAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                                     VK_ACCESS_SHADER_READ_BIT));
}

void RCGIPass::buildDepthPyramid(FrameContext& frameContext) {
    setDepthPyramidBarriersBefore(frameContext);
    VkCommandBuffer cmd = frameContext.commandBuffer;
    // Seed mip 0
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthPyramidSeedPipeline->getPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthPyramidPipelineLayout, 0, 1,
                            &frameContext.depthPyramidDescriptorSet, 0, nullptr);

    const uint32_t groupSizeX = 8;
    const uint32_t groupSizeY = 8;
    const uint32_t groupsX0 = (info.width + groupSizeX - 1) / groupSizeX;
    const uint32_t groupsY0 = (info.height + groupSizeY - 1) / groupSizeY;
    DepthPyramidPushConstants seedPC{};
    seedPC.cameraNear = frameContext.cameraData.nearPlane;
    seedPC.cameraFar = frameContext.cameraData.farPlane;
    seedPC.padding = 0.0f;

    vkCmdPushConstants(cmd, depthPyramidPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(DepthPyramidPushConstants), &seedPC);
    depthPyramidSeedPipeline->dispatch(cmd, groupsX0, groupsY0, 1);

    for (uint32_t m = 1; m < frameContext.depthPyramidMipLevels; ++m) {
        setMipLevelBarriers(frameContext, m);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthPyramidDownsamplePipeline->getPipeline());
        VkDescriptorSet setForMip = frameContext.depthPyramidMipDescriptorSets[m];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthPyramidPipelineLayout, 0, 1, &setForMip, 0,
                                nullptr);

        const uint32_t mipWidth = std::max(1u, info.width >> m);
        const uint32_t mipHeight = std::max(1u, info.height >> m);
        const uint32_t groupsX = (mipWidth + groupSizeX - 1) / groupSizeX;
        const uint32_t groupsY = (mipHeight + groupSizeY - 1) / groupSizeY;
        depthPyramidDownsamplePipeline->dispatch(cmd, groupsX, groupsY, 1);
    }

    setDepthPyramidCompletedBarriers(frameContext);
}

void RCGIPass::run(FrameContext& frameContext) {
    buildDepthPyramid(frameContext);
    if (mode == Mode::Cascades) {
        buildRCCascades(frameContext);
        mergeRCCascades(frameContext);
    } else if (mode == Mode::Reference) {
        traceReference(frameContext);
    }
    resolveIndirect(frameContext);
}

void RCGIPass::emitComputeBarrier(VkCommandBuffer cmd) const {
    cmdMemoryBarrier(
        cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        MemoryBarrierDesc{VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT});
}

} // namespace Rendering
