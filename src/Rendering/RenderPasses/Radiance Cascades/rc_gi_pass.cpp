#include "rc_gi_pass.hpp"

#include <algorithm>
#include <stdexcept>


namespace Rendering {
constexpr uint32_t RC_BUILD_GROUP_SIZE_X = 8u;
constexpr uint32_t RC_BUILD_GROUP_SIZE_Y = 8u;
RCGIPass::RCGIPass(Device& device, const CreateInfo& createInfo)
    : device(device), info(createInfo) {
    // Create a dedicated sampler for compute sampling
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;  // texelFetch ignores filter, but NEAREST is explicit
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &rcSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create RC compute sampler");
    }

    createDepthPyramidPipeline();
    createRCBuildPipeline();
    createRCMergePipeline();
    createRCResolvePipeline();
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
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = static_cast<uint32_t>(sizeof(DepthPyramidPushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &info.depthPyramidSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    if (vkCreatePipelineLayout(device.getDevice(), &layoutInfo, nullptr, &depthPyramidPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout for depth pyramid");
    }

    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = depthPyramidPipelineLayout;
    depthPyramidSeedPipeline = std::make_unique<ComputePipeline>(
        device,
        "shaders/rc_depth_copy.comp.spv",
        cfg
    );
    depthPyramidDownsamplePipeline = std::make_unique<ComputePipeline>(
        device,
        "shaders/rc_depth_downsample.comp.spv",
        cfg
    );
}

void RCGIPass::createRCBuildPipeline() {
    // Push constants: cascadeIndex, probeStridePx, tileSize, depthMipCount, frameIndex, tStart, segmentLen
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = static_cast<uint32_t>(sizeof(CascadeBuildPushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &info.rcBuildSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    if (vkCreatePipelineLayout(device.getDevice(), &layoutInfo, nullptr, &rcBuildPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout for RC build");
    }

    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = rcBuildPipelineLayout;
    rcBuildPipeline = std::make_unique<ComputePipeline>(
        device,
        "shaders/rc_build_cascade.comp.spv",
        cfg
    );
}

void RCGIPass::createRCMergePipeline() {
    if (rcBuildPipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = rcBuildPipelineLayout;
    rcMergePipeline = std::make_unique<ComputePipeline>(
        device,
        "shaders/rc_merge.comp.spv",
        cfg
    );
}

void RCGIPass::createRCResolvePipeline() {
    // Push constants: cascadeIndex,probeStridePx,tileSize,giIntensity
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = static_cast<uint32_t>(sizeof(ResolvePushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    std::array<VkDescriptorSetLayout, 2> setLayouts = {
        info.rcResolveSetLayout,
        info.skyboxSetLayout
    };

    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    if (vkCreatePipelineLayout(device.getDevice(), &layoutInfo, nullptr, &rcResolvePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout for RC resolve");
    }

    ComputePipelineConfigInfo cfg{};
    cfg.pipelineLayout = rcResolvePipelineLayout;
    rcResolvePipeline = std::make_unique<ComputePipeline>(
        device,
        "shaders/rc_resolve_indirect.comp.spv",
        cfg
    );
}

void RCGIPass::buildRCCascades(FrameContext& frameContext) {
    VkCommandBuffer cmd = frameContext.commandBuffer;
    if (!rcBuildPipeline) {
        return;
    }

    // Bind pipeline and descriptor set once
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rcBuildPipeline->getPipeline());
    VkDescriptorSet ds = frameContext.rcBuildDescriptorSet;
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        rcBuildPipelineLayout,
        0,
        1,
        &ds,
        0,
        nullptr
    );

    // Compute probe-relative distance bands so each cascade integrates a specific ray interval
    const uint32_t depthMipLevels = frameContext.depthPyramidMipLevels;
    if (depthMipLevels == 0u) {
        return;
    }

    const int frameIndexMod = static_cast<int>(frameContext.temporalFrameIndex % 1000u);
    for (uint32_t cascade = 0; cascade < Rendering::RC_CASCADE_COUNT; ++cascade) {
        CascadeDispatchInfo dispatchInfo = prepareCascadeDispatch(cascade, cascadeBands[cascade]);
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
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        rcBuildPipelineLayout,
        0,
        1,
        &ds,
        0,
        nullptr
    );

    for (int cascade = static_cast<int>(Rendering::RC_CASCADE_COUNT) - 2; cascade >= 0; --cascade) {
        CascadeDispatchInfo dispatchInfo = prepareCascadeDispatch(
            static_cast<uint32_t>(cascade),
            cascadeBands[static_cast<size_t>(cascade)]
        );
        dispatchInfo.push.depthMipCount = 0;
        dispatchInfo.push.frameIndex = static_cast<int>(frameContext.temporalFrameIndex % 1000u);
        if (dispatchInfo.groupsX == 0u || dispatchInfo.groupsY == 0u) {
            continue;
        }

        vkCmdPushConstants(
            cmd,
            rcBuildPipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            static_cast<uint32_t>(sizeof(CascadeBuildPushConstants)),
            &dispatchInfo.push
        );

        rcMergePipeline->dispatch(cmd, dispatchInfo.groupsX, dispatchInfo.groupsY, 1);

        emitComputeBarrier(cmd);

    }
}

void RCGIPass::computeCascadeBands(){
    // 4x branching: angular resolution quadruples each cascade (2x per dimension),
    // so interval length must also scale by 4x to maintain the penumbra condition.
    // 
    // Following tutorial formula:
    //   interval_scale(c) = 0 for c=0, else 4^c
    //   interval_range(c) = [scale(c), scale(c+1)] * base
    //
    // This gives (with base = RC_BASE_INTERVAL_LENGTH):
    // Cascade 0: [0, 4*base]           length = 4*base
    // Cascade 1: [4*base, 16*base]     length = 12*base
    // Cascade 2: [16*base, 64*base]    length = 48*base
    // Cascade 3: [64*base, 256*base]   length = 192*base
    // Cascade 4: [256*base, 1024*base] length = 768*base
    // Cascade 5: [1024*base, 4096*base] length = 3072*base
    //
    // Total max distance = 4^(N+1) * base = 4096 * base for 6 cascades
    // Example: if base = 0.25, cascade 5 reaches 1024 world units
    
    const float baseSegmentLength = Rendering::RC_BASE_INTERVAL_LENGTH;  
    const uint32_t cascadeCount = Rendering::RC_CASCADE_COUNT;
    
    for (uint32_t c = 0; c < cascadeCount; ++c) {
        // scale(c) = 4^c for c > 0, else 0
        // scale(c+1) = 4^(c+1)
        const uint32_t scaleStart = (c == 0) ? 0u : (1u << (2u * c));
        const uint32_t scaleEnd = 1u << (2u * (c + 1u));
        
        float start = baseSegmentLength * static_cast<float>(scaleStart);
        float end = baseSegmentLength * static_cast<float>(scaleEnd);

        cascadeBands[c].start = start;
        cascadeBands[c].length = end - start;
    }
}

RCGIPass::CascadeDispatchInfo RCGIPass::prepareCascadeDispatch(uint32_t cascadeIndex, const CascadeBand& band) const {
    CascadeDispatchInfo dispatch{};
    dispatch.push.cascadeIndex = static_cast<int>(cascadeIndex);
    dispatch.push.probeStridePx = std::max(1, static_cast<int>(Rendering::RC_PROBE_STRIDE0_PX) << cascadeIndex);
    dispatch.push.tileSize = std::max(1, static_cast<int>(Rendering::RC_BASE_TILE_SIZE) << cascadeIndex);
    dispatch.push.frameIndex = 0;
    dispatch.push.tStart = std::max(0.0f, band.start);
    
    const float prevLen = (cascadeIndex > 0u)
        ? std::max(0.0f, cascadeBands[cascadeIndex - 1u].length)
        : std::max(0.0f, band.length);
    const float overlap = prevLen * Rendering::RC_INTERVAL_OVERLAP_FRACTION;
    dispatch.push.segmentLen = std::max(0.0f, band.length + overlap);

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

    vkCmdPushConstants(
        cmd,
        rcBuildPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        static_cast<uint32_t>(sizeof(CascadeBuildPushConstants)),
        &dispatchInfo.push
    );

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
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        rcResolvePipelineLayout,
        0,
        1,
        &frameContext.rcResolveDescriptorSet,
        0,
        nullptr
    );

    // Bind skybox descriptor set (Set 1)
    if (frameContext.skyboxDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            rcResolvePipelineLayout,
            1,
            1,
            &frameContext.skyboxDescriptorSet,
            0,
            nullptr
        );
    }

    // Push constants: base values + temporal data
    ResolvePushConstants pc{};
    pc.probeStridePx = static_cast<int>(Rendering::RC_PROBE_STRIDE0_PX);
    pc.tileSize = static_cast<int>(Rendering::RC_BASE_TILE_SIZE);
    pc.temporalFrame = static_cast<int>(frameContext.temporalFrameIndex);
    pc.prevViewProj = frameContext.prevCameraData.viewProjectionMatrix;

    vkCmdPushConstants(
        cmd,
        rcResolvePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(ResolvePushConstants),
        &pc
    );

    // Dispatch over screen size (8x8 group size)
    const uint32_t groupSizeX = 8;
    const uint32_t groupSizeY = 8;
    uint32_t groupsX = (info.width + groupSizeX - 1) / groupSizeX;
    uint32_t groupsY = (info.height + groupSizeY - 1) / groupSizeY;

    rcResolvePipeline->dispatch(cmd, groupsX, groupsY, 1);
}

void RCGIPass::setDepthPyramidBarriersBefore(FrameContext& frameContext) {
    VkCommandBuffer cmd = frameContext.commandBuffer;

    // Make scene depth visible to COMPUTE sampling
    VkImageMemoryBarrier depthBarrier{};
    depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.image = frameContext.depthImage;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBarrier.subresourceRange.baseMipLevel = 0;
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.baseArrayLayer = 0;
    depthBarrier.subresourceRange.layerCount = 1;

    // Transition mip 0 to GENERAL for write
    VkImageMemoryBarrier mip0ToGeneral{};
    mip0ToGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    mip0ToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    mip0ToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    mip0ToGeneral.image = frameContext.depthPyramidImage;
    mip0ToGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    mip0ToGeneral.subresourceRange.baseMipLevel = 0;
    mip0ToGeneral.subresourceRange.levelCount = 1;
    mip0ToGeneral.subresourceRange.baseArrayLayer = 0;
    mip0ToGeneral.subresourceRange.layerCount = 1;
    // Previous writer was depth attachment; be explicit about the dependency.
    mip0ToGeneral.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    mip0ToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mip0ToGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    mip0ToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkImageMemoryBarrier, 2> barriers{depthBarrier, mip0ToGeneral};
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(barriers.size()), barriers.data()
    );
}


void RCGIPass::setMipLevelBarriers(FrameContext& frameContext, uint32_t mipLevel) {
    
    VkCommandBuffer cmd = frameContext.commandBuffer;
    VkImageMemoryBarrier barriers[2]{};           
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; // prev mip to READ_ONLY
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = frameContext.depthPyramidImage;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel =mipLevel - 1;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; // dst mip to GENERAL
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // conservative starting state
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = frameContext.depthPyramidImage;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[1].subresourceRange.baseMipLevel = mipLevel;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        2, barriers
    );
}


// Finalize: transition last mip to READ_ONLY for external sampling
void RCGIPass::setDepthPyramidCompletedBarriers(FrameContext& frameContext) {
    VkCommandBuffer cmd = frameContext.commandBuffer;

    uint32_t lastMip = frameContext.depthPyramidMipLevels - 1;
    VkImageMemoryBarrier lastToRead{};
    lastToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    lastToRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    lastToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    lastToRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    lastToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lastToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    lastToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    lastToRead.image = frameContext.depthPyramidImage;
    lastToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    lastToRead.subresourceRange.baseMipLevel = lastMip;
    lastToRead.subresourceRange.levelCount = 1;
    lastToRead.subresourceRange.baseArrayLayer = 0;
    lastToRead.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &lastToRead
    );
}

void RCGIPass::buildDepthPyramid(FrameContext& frameContext) {
    setDepthPyramidBarriersBefore(frameContext);
    VkCommandBuffer cmd = frameContext.commandBuffer;
    // Seed mip 0
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthPyramidSeedPipeline->getPipeline());
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        depthPyramidPipelineLayout,
        0,
        1,
        &frameContext.depthPyramidDescriptorSet,
        0,
        nullptr
    );

    const uint32_t groupSizeX = 8;
    const uint32_t groupSizeY = 8;
    const uint32_t groupsX0 = (info.width + groupSizeX - 1) / groupSizeX;
    const uint32_t groupsY0 = (info.height + groupSizeY - 1) / groupSizeY;
    DepthPyramidPushConstants seedPC{};
    seedPC.cameraNear = frameContext.cameraData.nearPlane;
    seedPC.cameraFar = frameContext.cameraData.farPlane;
    seedPC.padding = 0.0f;

    vkCmdPushConstants(
        cmd,
        depthPyramidPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(DepthPyramidPushConstants),
        &seedPC
    );
    depthPyramidSeedPipeline->dispatch(cmd, groupsX0, groupsY0, 1);

    
    for (uint32_t m = 1; m < frameContext.depthPyramidMipLevels; ++m) {

        setMipLevelBarriers(frameContext, m);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthPyramidDownsamplePipeline->getPipeline());
        VkDescriptorSet setForMip = frameContext.depthPyramidMipDescriptorSets[m];
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            depthPyramidPipelineLayout,
            0,
            1,
            &setForMip,
            0,
            nullptr
        );

        const uint32_t mipWidth = std::max(1u, info.width >> m);
        const uint32_t mipHeight = std::max(1u, info.height >> m);
        const uint32_t groupsX = (mipWidth + groupSizeX - 1) / groupSizeX;
        const uint32_t groupsY = (mipHeight + groupSizeY - 1) / groupSizeY;
        depthPyramidDownsamplePipeline->dispatch(cmd, groupsX, groupsY, 1);
    }

    setDepthPyramidCompletedBarriers(frameContext);
}

void RCGIPass::run(FrameContext& frameContext) {
    computeCascadeBands();
    buildDepthPyramid(frameContext);
    buildRCCascades(frameContext);
    mergeRCCascades(frameContext);
    resolveIndirect(frameContext);
}

void RCGIPass::emitComputeBarrier(VkCommandBuffer cmd) const {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1,
        &barrier,
        0,
        nullptr,
        0,
        nullptr
    );
}

} // namespace Rendering


