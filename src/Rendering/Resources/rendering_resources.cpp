#include "rendering_resources.hpp"
#include "Engine/log.hpp"
#include "Rendering/Core/barriers.hpp"
#include "Rendering/Core/debug_names.hpp"
#include "Rendering/Core/images.hpp"
#include "Rendering/Core/samplers.hpp"
#include <stdexcept>
#include <algorithm>
#include "external/smaa_textures/AreaTex.h"
#include "external/smaa_textures/SearchTex.h"

namespace Rendering {

RenderingResources::RenderingResources(Device& device, SwapChain& swapChain,
                                       VkDescriptorSetLayout materialDescriptorSetLayout)
    : device(device), swapChain(swapChain), materialDescriptorSetLayout(materialDescriptorSetLayout) {
    width = swapChain.getExtent().width;
    height = swapChain.getExtent().height;

    // Create GBuffer first (it determines its own formats)
    GBuffer::CreateInfo gBufferInfo{};
    gBufferInfo.width = width;
    gBufferInfo.height = height;
    gBuffer = std::make_unique<GBuffer>(device, gBufferInfo);

    // Find all resource formats (including getting them from GBuffer)
    findResourcesFormats();

    // Create depth resources
    createDepthResources();
    createDepthPyramidResources();
    createLightPassResources();
    createTransparencyResources();
    createGIResources();
    createRCAtlases();
    createPostProcessResources();
    createBuffers();
    createDescriptorPool(createDescriptorSetLayouts());
    loadSMAALUTTextures();
    createShadowMapResources();
    allocateDescriptorSets();
    writeBufferSets();
    writeGBufferSets();
    writeShadowMapSamplerSets();
    writeCompositionSets();
    writePostProcessSets();
    writeDepthPyramidSets();
    writeRCSets();

    // Bind the scene skybox, or a placeholder when the scene has none, so the descriptor is always valid
    initializeSkyboxFromScene();

    Log::info("RenderingResources created with ", width, "x", height);
}

RenderingResources::~RenderingResources() {
    vkDeviceWaitIdle(device.getDevice());

    // The plain handles; every other object is a member and is destroyed after this body, in reverse declaration
    // order (see the header).
    const VkDevice vkDevice = device.getDevice();
    vkDestroySampler(vkDevice, lightPassSampler, nullptr);
    vkDestroySampler(vkDevice, depthPyramidSampler, nullptr);
    vkDestroySampler(vkDevice, postProcessSampler, nullptr);

    vkDestroySampler(vkDevice, smaaAreaSampler, nullptr);
    vkDestroyImageView(vkDevice, smaaAreaView, nullptr);
    vkDestroyImage(vkDevice, smaaAreaImage, nullptr);
    vkFreeMemory(vkDevice, smaaAreaMemory, nullptr);
    vkDestroySampler(vkDevice, smaaSearchSampler, nullptr);
    vkDestroyImageView(vkDevice, smaaSearchView, nullptr);
    vkDestroyImage(vkDevice, smaaSearchImage, nullptr);
    vkFreeMemory(vkDevice, smaaSearchMemory, nullptr);
}

void RenderingResources::findResourcesFormats() {
    // Initialize depth format
    depthFormat = device.getDepthFormat();

    // Unified HDR format for lighting + post-processing chain
    hdrFormat = device.findSupportedFormat(
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT}, VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    // Get formats from GBuffer to ensure consistency
    positionFormat = gBuffer->getPositionFormat();
    normalFormat = gBuffer->getNormalFormat();
    albedoFormat = gBuffer->getAlbedoFormat();
    materialFormat = gBuffer->getMaterialFormat();

    revealageFormat =
        device.findSupportedFormat({VK_FORMAT_R8_UNORM, VK_FORMAT_R16_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    // Depth pyramid min/max float format (sampled + storage) — RG16/32
    depthPyramidFormat =
        device.findSupportedFormat({VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32G32_SFLOAT}, VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);

    // GI indirect buffer format (R16G16B16A16 preferred), must support color attachment, sampled, and storage
    giIndirectFormat = device.findSupportedFormat(
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT}, VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);

    // Post-processing intermediates stay in HDR; tonemap later when writing to the swapchain
    postProcessFormat = hdrFormat;

    // SMAA intermediates: ensure the chosen formats support color attachment + sampling
    smaaEdgeFormat =
        device.findSupportedFormat({VK_FORMAT_R8G8_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    smaaBlendFormat =
        device.findSupportedFormat({VK_FORMAT_R8G8B8A8_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    Log::debug("RenderingResources formats found:");
    Log::debug("  Depth: ", depthFormat);
    Log::debug("  Position: ", positionFormat);
    Log::debug("  Normal: ", normalFormat);
    Log::debug("  Albedo: ", albedoFormat);
    Log::debug("  Material: ", materialFormat);
    Log::debug("  Revealage: ", revealageFormat);
    Log::debug("  GI Indirect: ", giIndirectFormat);
    Log::debug("  Depth Pyramid: ", depthPyramidFormat);
}

void RenderingResources::createDepthResources() {
    FrameImage::CreateInfo info{};
    info.format = depthFormat;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.extent = {width, height};
    info.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    info.name = "Depth";
    depthImage = std::make_unique<FrameImage>(device, info);

    Log::debug("Depth resources created: ", width, "x", height, " format=", depthFormat);
}

void RenderingResources::createDepthPyramidResources() {
    auto computeMipLevels = [&](uint32_t w, uint32_t h) -> uint32_t {
        uint32_t maxDim = std::max(w, h);
        uint32_t levels = 1;
        while ((maxDim >>= 1) > 0) {
            ++levels;
        }
        return levels;
    };

    const uint32_t requested = RC_DEPTH_MIP_LEVELS;
    const uint32_t maxPossible = computeMipLevels(width, height);
    const uint32_t mipLevels = requested == 0 ? maxPossible : std::min(requested, maxPossible);

    // Per-mip views for all mips (0..mipLevels-1) are used for both storage and sampling.
    FrameImage::CreateInfo info{};
    info.format = depthPyramidFormat;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.extent = {width, height};
    info.mipLevels = mipLevels;
    info.perMipViews = true;
    info.name = "DepthPyramid";
    depthPyramidImage = std::make_unique<FrameImage>(device, info);

    // One-time init: transition all pyramid images (all mips) to READ_ONLY so passes can assume a known starting layout
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         ImageBarrierDesc::undefinedToShaderRead(depthPyramidImage->image(i), VK_IMAGE_ASPECT_COLOR_BIT,
                                                                 1, mipLevels));
    }
    device.endSingleTimeCommands(cmd);

    // Create a dedicated sampler for depth pyramid sampling.
    // IMPORTANT: depth comparisons must be done with point sampling to avoid mixing geometry depth
    // with far-plane ("sky") depth near edges, which produces banding/missing-hit artifacts.
    depthPyramidSampler = createSampler(device.getDevice(), VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
                                        static_cast<float>(mipLevels));
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_SAMPLER, (uint64_t)depthPyramidSampler, "DepthPyramidSampler");

    Log::debug("Depth pyramid created with ", mipLevels, " mips at ", width, "x", height);
}

void RenderingResources::createLightPassResources() {
    // Create a sampler for the light pass result
    lightPassSampler = createSampler(device.getDevice(), VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, 0.0f);

    // Light pass render target, then the incident diffuse buffer (pre-albedo) with the same format and usages.
    FrameImage::CreateInfo info{};
    info.format = hdrFormat;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.extent = {width, height};
    info.name = "LightPass";
    lightPassResultImage = std::make_unique<FrameImage>(device, info);
    info.name = "LightIncident";
    lightIncidentImage = std::make_unique<FrameImage>(device, info);

    setDebugName(device.getDevice(), VK_OBJECT_TYPE_SAMPLER, (uint64_t)lightPassSampler, "LightPassSampler");
}

void RenderingResources::createBuffers() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        cameraUniformBuffers[i] =
            std::make_unique<Buffer>(device, sizeof(CameraUbo), 1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        cameraUniformBuffers[i]->map();

        modelMatrixBuffers[i] = std::make_unique<Buffer>(
            device, sizeof(glm::mat4), BASE_INSTANCED_RENDERABLES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        modelMatrixBuffers[i]->map();

        // Create normal matrix buffer
        normalMatrixBuffers[i] = std::make_unique<Buffer>(
            device, sizeof(glm::mat4), BASE_INSTANCED_RENDERABLES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        normalMatrixBuffers[i]->map();

        // Set debug names for buffers
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_BUFFER, (uint64_t)cameraUniformBuffers[i]->getBuffer(),
                     "CameraUniformBuffer_Frame" + std::to_string(i));
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_BUFFER, (uint64_t)modelMatrixBuffers[i]->getBuffer(),
                     "ModelMatrixBuffer_Frame" + std::to_string(i));
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_BUFFER, (uint64_t)normalMatrixBuffers[i]->getBuffer(),
                     "NormalMatrixBuffer_Frame" + std::to_string(i));
    }

    VkDeviceSize unifiedLightBufferSize = sizeof(UnifiedLightBuffer);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        lightArrayUniformBuffers[i] =
            std::make_unique<Buffer>(device, unifiedLightBufferSize, 1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        lightArrayUniformBuffers[i]->map();
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_BUFFER, (uint64_t)lightArrayUniformBuffers[i]->getBuffer(),
                     "LightArrayUniformBuffer_Frame" + std::to_string(i));
    }

    // Add cascade splits buffer creation
    VkDeviceSize cascadeSplitsBufferSize = sizeof(DirectionalLightCascadesBuffer);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        cascadeSplitsBuffers[i] =
            std::make_unique<Buffer>(device, cascadeSplitsBufferSize, 1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        cascadeSplitsBuffers[i]->map();
    }

    VkDeviceSize sceneLightingBufferSize = sizeof(SceneLightingUbo);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        sceneLightingBuffers[i] =
            std::make_unique<Buffer>(device, sceneLightingBufferSize, 1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        sceneLightingBuffers[i]->map();
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_BUFFER, (uint64_t)sceneLightingBuffers[i]->getBuffer(),
                     "SceneLightingBuffer_Frame" + std::to_string(i));
    }

    VkDeviceSize bufferSize = sizeof(ShadowcastingLightMatrices);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        lightMatrixBuffers[i] =
            std::make_unique<Buffer>(device, bufferSize, 1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        lightMatrixBuffers[i]->map();
    }

    VkDeviceSize shadowModelMatrixBuffer = sizeof(glm::mat4);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        shadowModelMatrixBuffers[i] = std::make_unique<Buffer>(
            device, shadowModelMatrixBuffer, BASE_INSTANCED_RENDERABLES * MAX_SHADOW_CASCADE_COUNT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        shadowModelMatrixBuffers[i]->map();
    }

    VkDeviceSize matrixBufferSize = sizeof(glm::mat4);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        transparencyModelMatrixBuffers[i] = std::make_unique<Buffer>(
            device, matrixBufferSize, BASE_INSTANCED_RENDERABLES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        transparencyModelMatrixBuffers[i]->map();

        transparencyNormalMatrixBuffers[i] = std::make_unique<Buffer>(
            device, matrixBufferSize, BASE_INSTANCED_RENDERABLES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        transparencyNormalMatrixBuffers[i]->map();
    }
}

void RenderingResources::createShadowMapResources() {
    // Create shadow maps for directional lights - one per frame per light
    for (size_t lightIndex = 0; lightIndex < MAX_DIRECTIONAL_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            ShadowMap::ShadowMapCreateInfo createInfo{};
            createInfo.width = DIRECTIONAL_SHADOW_MAP_RES;
            createInfo.height = DIRECTIONAL_SHADOW_MAP_RES;
            createInfo.arrayLayers = MAX_SHADOW_CASCADE_COUNT;
            createInfo.depthFormat = depthFormat;
            directionalMaps[lightIndex][frameIndex] = std::make_unique<ShadowMap>(device, createInfo);
        }
    }

    // Create shadow maps for spot lights - one per frame per light
    for (size_t lightIndex = 0; lightIndex < MAX_SPOT_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            ShadowMap::ShadowMapCreateInfo createInfo{};
            createInfo.width = SPOT_SHADOW_MAP_RES;
            createInfo.height = SPOT_SHADOW_MAP_RES;
            createInfo.arrayLayers = 1;
            createInfo.depthFormat = depthFormat;
            spotlightMaps[lightIndex][frameIndex] = std::make_unique<ShadowMap>(device, createInfo);
        }
    }

    // Create shadow maps for point lights (cubemaps) - one per frame per light
    for (size_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            ShadowMap::ShadowMapCreateInfo createInfo{};
            createInfo.width = POINT_SHADOW_MAP_RES;
            createInfo.height = POINT_SHADOW_MAP_RES;
            createInfo.arrayLayers = 6;
            createInfo.depthFormat = depthFormat;

            pointlightMaps[lightIndex][frameIndex] = std::make_unique<ShadowMap>(device, createInfo);
        }
    }
}

void RenderingResources::createTransparencyResources() {
    FrameImage::CreateInfo info{};
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.extent = {width, height};

    info.format = hdrFormat;
    info.name = "Accumulation";
    accumulationImage = std::make_unique<FrameImage>(device, info);

    info.format = revealageFormat; // R8
    info.name = "Revealage";
    revealageImage = std::make_unique<FrameImage>(device, info);
}

void RenderingResources::createGIResources() {
    FrameImage::CreateInfo info{};
    info.format = giIndirectFormat;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    info.extent = {width, height};
    info.name = "GIIndirect";
    giIndirectImage = std::make_unique<FrameImage>(device, info);

    // One-time init: transition GI images to GENERAL for compute writes
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         ImageBarrierDesc::undefinedToGeneralShaderWrite(giIndirectImage->image(i)));
    }
    device.endSingleTimeCommands(cmd);
}

void RenderingResources::createPostProcessResources() {
    // Shared sampler for post-process textures (linear clamp)
    postProcessSampler = createSampler(device.getDevice(), VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, 0.0f);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_SAMPLER, (uint64_t)postProcessSampler, "PostProcessSampler");

    auto makeColorImage = [&](VkFormat format, const std::string& name) {
        FrameImage::CreateInfo info{};
        info.format = format;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.extent = {width, height};
        info.name = name;
        return std::make_unique<FrameImage>(device, info);
    };

    compositionColorImage = makeColorImage(postProcessFormat, "CompositionColor");
    smaaEdgeImage = makeColorImage(smaaEdgeFormat, "SMAAEdge");
    smaaBlendImage = makeColorImage(smaaBlendFormat, "SMAABlend");
    postAAColorImage = makeColorImage(postProcessFormat, "PostAAColor");
}

void RenderingResources::loadSMAALUTTextures() {
    // Helper lambda for creating SMAA LUT images (no mipmaps, no transfer src)
    auto createSMAAImage = [&](uint32_t w, uint32_t h, VkFormat format, const void* data, VkImage& outImage,
                               VkDeviceMemory& outMemory, VkImageView& outView, const std::string& name) {
        VkDeviceSize imageSize = w * h * ((format == VK_FORMAT_R8G8_UNORM) ? 2 : 1);

        // Create staging buffer
        Buffer stagingBuffer{device, imageSize, 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        stagingBuffer.map(imageSize);
        stagingBuffer.writeToBuffer(data, imageSize);

        // Create image with exactly 1 mip level
        // Critical: no mipmaps for SMAA LUTs
        VkImageCreateInfo imageInfo =
            imageCreateInfo2D(w, h, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outImage, outMemory);

        // Transition to transfer destination
        VkCommandBuffer cmd = device.beginSingleTimeCommands();

        cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         ImageBarrierDesc{outImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT});

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {w, h, 1};

        vkCmdCopyBufferToImage(cmd, stagingBuffer.getBuffer(), outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);

        // Transition to shader read optimal
        cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         ImageBarrierDesc{outImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                                          VK_ACCESS_SHADER_READ_BIT});

        device.endSingleTimeCommands(cmd);

        // Create image view
        outView = createImageView(device.getDevice(), outImage, format);

        setDebugName(device.getDevice(), VK_OBJECT_TYPE_IMAGE, (uint64_t)outImage, name + "_Image");
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)outView, name + "_View");
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)outMemory, name + "_Memory");
    };

    // Create Area texture (160x560, R8G8, linear filtering, clamp)
    createSMAAImage(AREATEX_WIDTH, AREATEX_HEIGHT, VK_FORMAT_R8G8_UNORM, areaTexBytes, smaaAreaImage, smaaAreaMemory,
                    smaaAreaView, "SMAA_Area");

    // Create sampler for Area texture: LINEAR filtering, CLAMP_TO_EDGE
    {
        smaaAreaSampler =
            createSampler(device.getDevice(), VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_SAMPLER, (uint64_t)smaaAreaSampler, "SMAA_Area_Sampler");
    }

    // Create Search texture (64x16, R8, POINT/NEAREST filtering, clamp)
    createSMAAImage(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, VK_FORMAT_R8_UNORM, searchTexBytes, smaaSearchImage,
                    smaaSearchMemory, smaaSearchView, "SMAA_Search");

    // Create sampler for Search texture: NEAREST filtering, CLAMP_TO_EDGE
    // The search texture must use point sampling for correct lookups
    {
        // Critical: POINT sampling
        smaaSearchSampler =
            createSampler(device.getDevice(), VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_SAMPLER, (uint64_t)smaaSearchSampler, "SMAA_Search_Sampler");
    }

    Log::debug("SMAA LUT textures loaded (Area: ", AREATEX_WIDTH, "x", AREATEX_HEIGHT, ", Search: ", SEARCHTEX_WIDTH,
               "x", SEARCHTEX_HEIGHT, ")");
}

void RenderingResources::createRCAtlases() {
    // Allocate per-cascade atlases (radiance: R16G16B16A16; β packed into alpha)
    for (uint32_t cascade = 0; cascade < RC_CASCADE_COUNT; ++cascade) {
        const uint32_t stridePx = RC_PROBE_STRIDE0_PX << cascade; // Δp_i = 2^i
        const uint32_t tileSize = RC_BASE_TILE_SIZE << cascade;   // tile_i = base * 2^i

        const uint32_t probesX = (width + stridePx - 1) / stridePx;
        const uint32_t probesY = (height + stridePx - 1) / stridePx;

        FrameImage::CreateInfo info{};
        info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.extent = {std::max(1u, probesX * tileSize), std::max(1u, probesY * tileSize)};
        info.name = "RCRadianceCascade" + std::to_string(cascade);
        rcRadianceImages[cascade] = std::make_unique<FrameImage>(device, info);

        Log::debug("RC atlas c=", cascade, " size=", info.extent.width, "x", info.extent.height, " stridePx=", stridePx,
                   " tile=", tileSize);
    }

    // One-time init: transition all RC atlas images to GENERAL for compute writes
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (uint32_t cascade = 0; cascade < RC_CASCADE_COUNT; ++cascade) {
        for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             ImageBarrierDesc::undefinedToGeneralShaderWrite(rcRadianceImages[cascade]->image(frame)));
        }
    }
    device.endSingleTimeCommands(cmd);
}

} // namespace Rendering