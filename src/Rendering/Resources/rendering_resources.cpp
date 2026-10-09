#include "rendering_resources.hpp"
#include "Engine/log.hpp"
#include "Rendering/Core/barriers.hpp"
#include "Rendering/Core/debug_names.hpp"
#include "Rendering/Core/images.hpp"
#include "Rendering/Core/samplers.hpp"
#include <stdexcept>
#include <algorithm>
#include <tuple>
#include <type_traits>
#include "Scene/scene.hpp"
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
    cleanup();
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

void RenderingResources::cleanup() {
    // Wait for device to be idle before cleanup
    vkDeviceWaitIdle(device.getDevice());

    // Clean up descriptor set layouts
    if (modelsDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), modelsDescriptorSetLayout, nullptr);
        modelsDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (cameraDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), cameraDescriptorSetLayout, nullptr);
        cameraDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (gBufferDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), gBufferDescriptorSetLayout, nullptr);
        gBufferDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (lightArrayDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), lightArrayDescriptorSetLayout, nullptr);
        lightArrayDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (cascadeSplitsSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), cascadeSplitsSetLayout, nullptr);
        cascadeSplitsSetLayout = VK_NULL_HANDLE;
    }
    if (sceneLightingDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), sceneLightingDescriptorSetLayout, nullptr);
        sceneLightingDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (shadowcastinglightMatrixDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), shadowcastinglightMatrixDescriptorSetLayout, nullptr);
        shadowcastinglightMatrixDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (shadowModelMatrixDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), shadowModelMatrixDescriptorSetLayout, nullptr);
        shadowModelMatrixDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (shadowMapSamplerLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), shadowMapSamplerLayout, nullptr);
        shadowMapSamplerLayout = VK_NULL_HANDLE;
    }
    if (skyboxDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), skyboxDescriptorSetLayout, nullptr);
        skyboxDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (transparencyModelDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), transparencyModelDescriptorSetLayout, nullptr);
        transparencyModelDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (compositionSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), compositionSetLayout, nullptr);
        compositionSetLayout = VK_NULL_HANDLE;
    }
    if (smaaEdgeSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), smaaEdgeSetLayout, nullptr);
        smaaEdgeSetLayout = VK_NULL_HANDLE;
    }
    if (smaaWeightSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), smaaWeightSetLayout, nullptr);
        smaaWeightSetLayout = VK_NULL_HANDLE;
    }
    if (smaaBlendSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), smaaBlendSetLayout, nullptr);
        smaaBlendSetLayout = VK_NULL_HANDLE;
    }
    if (colorCorrectionSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), colorCorrectionSetLayout, nullptr);
        colorCorrectionSetLayout = VK_NULL_HANDLE;
    }
    if (rcBuildSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), rcBuildSetLayout, nullptr);
        rcBuildSetLayout = VK_NULL_HANDLE;
    }
    if (rcResolveSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), rcResolveSetLayout, nullptr);
        rcResolveSetLayout = VK_NULL_HANDLE;
    }
    if (depthPyramidSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), depthPyramidSetLayout, nullptr);
        depthPyramidSetLayout = VK_NULL_HANDLE;
    }

    // Clean up samplers
    if (lightPassSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), lightPassSampler, nullptr);
        lightPassSampler = VK_NULL_HANDLE;
    }
    if (depthPyramidSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), depthPyramidSampler, nullptr);
        depthPyramidSampler = VK_NULL_HANDLE;
    }
    if (postProcessSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), postProcessSampler, nullptr);
        postProcessSampler = VK_NULL_HANDLE;
    }

    // Clean up SMAA LUT textures
    if (smaaAreaSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), smaaAreaSampler, nullptr);
        smaaAreaSampler = VK_NULL_HANDLE;
    }
    if (smaaAreaView != VK_NULL_HANDLE) {
        vkDestroyImageView(device.getDevice(), smaaAreaView, nullptr);
        smaaAreaView = VK_NULL_HANDLE;
    }
    if (smaaAreaImage != VK_NULL_HANDLE) {
        vkDestroyImage(device.getDevice(), smaaAreaImage, nullptr);
        smaaAreaImage = VK_NULL_HANDLE;
    }
    if (smaaAreaMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device.getDevice(), smaaAreaMemory, nullptr);
        smaaAreaMemory = VK_NULL_HANDLE;
    }
    if (smaaSearchSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), smaaSearchSampler, nullptr);
        smaaSearchSampler = VK_NULL_HANDLE;
    }
    if (smaaSearchView != VK_NULL_HANDLE) {
        vkDestroyImageView(device.getDevice(), smaaSearchView, nullptr);
        smaaSearchView = VK_NULL_HANDLE;
    }
    if (smaaSearchImage != VK_NULL_HANDLE) {
        vkDestroyImage(device.getDevice(), smaaSearchImage, nullptr);
        smaaSearchImage = VK_NULL_HANDLE;
    }
    if (smaaSearchMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device.getDevice(), smaaSearchMemory, nullptr);
        smaaSearchMemory = VK_NULL_HANDLE;
    }

    // Per-frame images, in the order they were destroyed before FrameImage
    depthImage.reset();
    depthPyramidImage.reset();
    lightPassResultImage.reset();
    lightIncidentImage.reset();
    accumulationImage.reset();
    revealageImage.reset();
    giIndirectImage.reset();
    compositionColorImage.reset();
    smaaEdgeImage.reset();
    smaaBlendImage.reset();
    postAAColorImage.reset();
    for (auto& rcRadianceImage : rcRadianceImages) {
        rcRadianceImage.reset();
    }

    // Clean up shadow maps - now per frame per light
    for (size_t lightIndex = 0; lightIndex < MAX_DIRECTIONAL_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            directionalMaps[lightIndex][frameIndex].reset();
        }
    }
    for (size_t lightIndex = 0; lightIndex < MAX_SPOT_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            spotlightMaps[lightIndex][frameIndex].reset();
        }
    }
    for (size_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; lightIndex++) {
        for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
            pointlightMaps[lightIndex][frameIndex].reset();
        }
    }

    // Clean up buffers (unique_ptr will handle destruction automatically)
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        cameraUniformBuffers[i].reset();
        modelMatrixBuffers[i].reset();
        normalMatrixBuffers[i].reset();
        lightArrayUniformBuffers[i].reset();
        cascadeSplitsBuffers[i].reset();
        sceneLightingBuffers[i].reset();
        lightMatrixBuffers[i].reset();
        shadowModelMatrixBuffers[i].reset();
        transparencyModelMatrixBuffers[i].reset();
        transparencyNormalMatrixBuffers[i].reset();
    }

    // Clean up GBuffer (unique_ptr will handle destruction automatically)
    gBuffer.reset();

    // Clean up descriptor pool (unique_ptr will handle destruction automatically)
    descriptorPool.reset();
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

void RenderingResources::createDescriptorPool(const DescriptorBudget& budget) {
    const std::vector<VkDescriptorPoolSize> poolSizes = budget.poolSizes();
    for (const VkDescriptorPoolSize& size : poolSizes) {
        Log::debug("Pool size: type ", size.type, " x", size.descriptorCount);
    }
    Log::debug("Pool sets: ", budget.maxSets());

    descriptorPool = DescriptorPool::Builder(device).fromBudget(budget).build();
}

DescriptorBudget RenderingResources::createDescriptorSetLayouts() {
    // Every layout states how many sets allocateDescriptorSets allocates from it; the pool is sized from these.
    // The depth pyramid has one set per mip level of each frame (the seed set is mip 0).
    const uint32_t depthPyramidSetCount = MAX_FRAMES_IN_FLIGHT * depthPyramidImage->mipLevels();
    DescriptorBudget budget;

    // Create descriptor set layout for instance storage buffers
    modelsDescriptorSetLayout =
        createDescriptorSetLayout(device,
                                  {
                                      layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                                      layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                                  },
                                  budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)modelsDescriptorSetLayout,
                 "ModelsDescriptorSetLayout");

    //Create descriptor set layout for camera uniform buffer
    cameraDescriptorSetLayout =
        createDescriptorSetLayout(device,
                                  {
                                      layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
                                  },
                                  budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)cameraDescriptorSetLayout,
                 "CameraDescriptorSetLayout");

    gBufferDescriptorSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)gBufferDescriptorSetLayout,
                 "GBufferDescriptorSetLayout");

    lightArrayDescriptorSetLayout =
        createDescriptorSetLayout(device,
                                  {
                                      layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                  },
                                  budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)lightArrayDescriptorSetLayout,
                 "LightArrayDescriptorSetLayout");

    cascadeSplitsSetLayout =
        createDescriptorSetLayout(device,
                                  {
                                      layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                  },
                                  budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)cascadeSplitsSetLayout,
                 "CascadeSplitsDescriptorSetLayout");

    sceneLightingDescriptorSetLayout =
        createDescriptorSetLayout(device,
                                  {
                                      layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                  },
                                  budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)sceneLightingDescriptorSetLayout,
                 "SceneLightingDescriptorSetLayout");

    // Create descriptor set layout (shared between both pipelines)
    shadowcastinglightMatrixDescriptorSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                 (uint64_t)shadowcastinglightMatrixDescriptorSetLayout, "ShadowLightMatrixDescriptorSetLayout");

    // Create descriptor set layout for shadow map samplers
    shadowMapSamplerLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                          MAX_DIRECTIONAL_LIGHTS),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_SPOT_LIGHTS),
            layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_POINT_LIGHTS),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)shadowMapSamplerLayout,
                 "ShadowMapSamplerDescriptorSetLayout");

    //Create descriptor set layout for shadow model matrix
    shadowModelMatrixDescriptorSetLayout =
        createDescriptorSetLayout(device,
                                  {
                                      layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                                  },
                                  budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                 (uint64_t)shadowModelMatrixDescriptorSetLayout, "ShadowModelMatrixDescriptorSetLayout");

    //Create descriptor set layout for skybox
    skyboxDescriptorSetLayout =
        createDescriptorSetLayout(device,
                                  {
                                      layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                    VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT),
                                  },
                                  budget, 1);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)skyboxDescriptorSetLayout,
                 "SkyboxDescriptorSetLayout");

    //Create descriptor set layout for transparency model matrix
    transparencyModelDescriptorSetLayout =
        createDescriptorSetLayout(device,
                                  {
                                      layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                                      layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                                  },
                                  budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                 (uint64_t)transparencyModelDescriptorSetLayout, "TransparencyModelDescriptorSetLayout");

    // Create descriptor set layout for composition textures
    compositionSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)compositionSetLayout,
                 "CompositionDescriptorSetLayout");

    // SMAA edge descriptor set layout (compositionColor input)
    smaaEdgeSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaEdgeSetLayout,
                 "SMAAEdgeDescriptorSetLayout");

    // SMAA weight descriptor set layout (edges + area/search LUT)
    smaaWeightSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaWeightSetLayout,
                 "SMAAWeightDescriptorSetLayout");

    // SMAA blend descriptor set layout (compositionColor + blend weights)
    smaaBlendSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaBlendSetLayout,
                 "SMAABlendDescriptorSetLayout");

    // Color correction descriptor set layout (post-AA color input)
    colorCorrectionSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)colorCorrectionSetLayout,
                 "ColorCorrectionDescriptorSetLayout");

    // RC Build descriptor set layout
    // NOTE: β is packed into uRadiance alpha (radiance.rgb, beta.a), so we only need one storage atlas array.
    rcBuildSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, RC_CASCADE_COUNT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)rcBuildSetLayout,
                 "RCBuildDescriptorSetLayout");

    // RC Resolve descriptor set layout
    // NOTE: keep binding numbers stable (skip binding 6) to avoid shifting shader bindings:
    // binding 5 = radiance (rgba16f, beta in alpha), binding 7 = gi out, binding 8/9/10 = history/prev pos/prev normal.
    rcResolveSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, RC_CASCADE_COUNT),
            layoutBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            layoutBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
        },
        budget, MAX_FRAMES_IN_FLIGHT);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)rcResolveSetLayout,
                 "RCResolveDescriptorSetLayout");

    // Depth pyramid build descriptor set layout (centralized)
    depthPyramidSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT),
        },
        budget, depthPyramidSetCount);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)depthPyramidSetLayout,
                 "DepthPyramidDescriptorSetLayout");

    return budget;
}

void RenderingResources::allocateDescriptorSets() {
    // One family of sets at a time, in the order createDescriptorSetLayouts creates their layouts; every frame of a
    // family is allocated before the next family. An empty name leaves the sets unnamed.
    auto allocateFrames = [this](std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT>& sets, VkDescriptorSetLayout layout,
                                 const std::string& name) {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            sets[i] = allocateDescriptorSet(*descriptorPool, layout);
            if (!name.empty()) {
                setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)sets[i],
                             name + "_Frame" + std::to_string(i));
            }
        }
    };

    allocateFrames(modelsDescriptorSets, modelsDescriptorSetLayout, "ModelsDescriptorSet");
    allocateFrames(cameraDescriptorSets, cameraDescriptorSetLayout, "CameraDescriptorSet");
    allocateFrames(gBufferDescriptorSets, gBufferDescriptorSetLayout, "GBufferDescriptorSet");
    allocateFrames(lightArrayDescriptorSets, lightArrayDescriptorSetLayout, "LightArrayDescriptorSet");
    allocateFrames(cascadeSplitsDescriptorSets, cascadeSplitsSetLayout, "");
    allocateFrames(sceneLightingDescriptorSets, sceneLightingDescriptorSetLayout, "");
    allocateFrames(lightMatrixDescriptorSets, shadowcastinglightMatrixDescriptorSetLayout, "");
    allocateFrames(shadowMapSamplerSets, shadowMapSamplerLayout, "");
    allocateFrames(shadowModelMatrixDescriptorSets, shadowModelMatrixDescriptorSetLayout, "");

    // Skybox: a single set, not per frame. updateSkyboxDescriptorSet writes it.
    skyboxDescriptorSet = allocateDescriptorSet(*descriptorPool, skyboxDescriptorSetLayout);
    setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)skyboxDescriptorSet,
                 "SkyboxDescriptorSet");

    allocateFrames(transparencyModelMatrixDescriptorSets, transparencyModelDescriptorSetLayout, "");
    allocateFrames(compositionDescriptorSets, compositionSetLayout, "CompositionDescriptorSet");
    allocateFrames(smaaEdgeDescriptorSets, smaaEdgeSetLayout, "SMAAEdgeDescriptorSet");
    allocateFrames(smaaWeightDescriptorSets, smaaWeightSetLayout, "SMAAWeightDescriptorSet");
    allocateFrames(smaaBlendDescriptorSets, smaaBlendSetLayout, "SMAABlendDescriptorSet");
    allocateFrames(colorCorrectionDescriptorSets, colorCorrectionSetLayout, "ColorCorrectionDescriptorSet");
    allocateFrames(rcBuildDescriptorSets, rcBuildSetLayout, "RCBuildDescriptorSet");
    allocateFrames(rcResolveDescriptorSets, rcResolveSetLayout, "RCResolveDescriptorSet");

    // Depth pyramid: per frame, the seed set (depth -> mip 0), then one set per mip 1..N-1 for the downsample loop.
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        depthPyramidDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, depthPyramidSetLayout);
        setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)depthPyramidDescriptorSets[i],
                     "DepthPyramidDescriptorSet_Frame" + std::to_string(i));

        depthPyramidMipDescriptorSets[i].resize(depthPyramidImage->mipLevels());
        for (uint32_t m = 1; m < depthPyramidImage->mipLevels(); ++m) {
            depthPyramidMipDescriptorSets[i][m] = allocateDescriptorSet(*descriptorPool, depthPyramidSetLayout);
            setDebugName(device.getDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET,
                         (uint64_t)depthPyramidMipDescriptorSets[i][m],
                         "DepthPyramidMipDescriptorSet_Frame" + std::to_string(i) + "_Mip" + std::to_string(m));
        }
    }
}

void RenderingResources::writeBufferSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Instance buffers
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, modelMatrixBuffers[i]->descriptorInfo())
            .buffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, normalMatrixBuffers[i]->descriptorInfo())
            .update(device, modelsDescriptorSets[i]);

        // Camera buffer
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo())
            .update(device, cameraDescriptorSets[i]);

        // Light array buffer
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    lightArrayUniformBuffers[i]->descriptorInfo(lightArrayUniformBuffers[i]->getBufferSize()))
            .update(device, lightArrayDescriptorSets[i]);

        // Cascade splits buffer
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    cascadeSplitsBuffers[i]->descriptorInfo(cascadeSplitsBuffers[i]->getBufferSize()))
            .update(device, cascadeSplitsDescriptorSets[i]);

        // Scene lighting buffer
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    sceneLightingBuffers[i]->descriptorInfo(sceneLightingBuffers[i]->getBufferSize()))
            .update(device, sceneLightingDescriptorSets[i]);

        // Light matrices
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    lightMatrixBuffers[i]->descriptorInfo(lightMatrixBuffers[i]->getBufferSize()))
            .update(device, lightMatrixDescriptorSets[i]);

        // Shadow model matrices
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    shadowModelMatrixBuffers[i]->descriptorInfo(shadowModelMatrixBuffers[i]->getBufferSize()))
            .update(device, shadowModelMatrixDescriptorSets[i]);

        // Transparency model matrices
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, transparencyModelMatrixBuffers[i]->descriptorInfo())
            .buffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, transparencyNormalMatrixBuffers[i]->descriptorInfo())
            .update(device, transparencyModelMatrixDescriptorSets[i]);
    }
}

void RenderingResources::writeGBufferSets() {
    const VkSampler gBufferSampler = gBuffer->getSampler();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBufferSampler, gBuffer->getPositionView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBufferSampler, gBuffer->getNormalView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBufferSampler, gBuffer->getAlbedoView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBufferSampler, gBuffer->getMaterialView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, gBufferDescriptorSets[i]);
    }
}

void RenderingResources::writeShadowMapSamplerSets() {
    // Each frame's set holds that frame's shadow maps.
    for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
        // createShadowMapResources made every shadow map of every frame, so each array is written in full.
        auto imageInfos = [frameIndex](const auto& maps) {
            std::array<VkDescriptorImageInfo, std::tuple_size_v<std::decay_t<decltype(maps)>>> infos{};
            for (size_t lightIndex = 0; lightIndex < infos.size(); lightIndex++) {
                const ShadowMap& map = *maps[lightIndex][frameIndex];
                infos[lightIndex] = {map.getSampler(), map.getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            }
            return infos;
        };
        const auto directionalImageInfos = imageInfos(directionalMaps);
        const auto spotImageInfos = imageInfos(spotlightMaps);
        const auto pointImageInfos = imageInfos(pointlightMaps);

        DescriptorWriter()
            .images(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, directionalImageInfos.data(),
                    static_cast<uint32_t>(directionalImageInfos.size()))
            .images(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, spotImageInfos.data(),
                    static_cast<uint32_t>(spotImageInfos.size()))
            .images(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, pointImageInfos.data(),
                    static_cast<uint32_t>(pointImageInfos.size()))
            .update(device, shadowMapSamplerSets[frameIndex]);
    }
}

void RenderingResources::writeCompositionSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // The indirect GI buffer stays in GENERAL because RCGI computes and readers share it within one frame.
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, lightPassResultImage->view(i),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // opaque result of the light pass
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, accumulationImage->view(i),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // transparency accumulation
            .image(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, revealageImage->view(i),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // transparency revealage
            .image(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, giIndirectImage->view(i), VK_IMAGE_LAYOUT_GENERAL}) // indirect GI
            .update(device, compositionDescriptorSets[i]);
    }
}

void RenderingResources::writePostProcessSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // SMAA edge pass
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, compositionColorImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaEdgeDescriptorSets[i]);

        // SMAA weight pass
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, smaaEdgeImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {smaaAreaSampler, smaaAreaView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {smaaSearchSampler, smaaSearchView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaWeightDescriptorSets[i]);

        // SMAA blend pass
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, compositionColorImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, smaaBlendImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaBlendDescriptorSets[i]);

        // Color correction pass
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, postAAColorImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, colorCorrectionDescriptorSets[i]);
    }
}

void RenderingResources::writeDepthPyramidSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Seed set: source depth is point sampled; the destination is pyramid mip 0 as a storage image,
        // transitioned to GENERAL before the dispatch.
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {depthPyramidSampler, depthImage->view(i), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                   {VK_NULL_HANDLE, depthPyramidImage->mipView(i, 0), VK_IMAGE_LAYOUT_GENERAL})
            .update(device, depthPyramidDescriptorSets[i]);

        for (uint32_t m = 1; m < depthPyramidImage->mipLevels(); ++m) {
            // Source: previous mip level (m-1), sampled. It is in SHADER_READ_ONLY_OPTIMAL at dispatch time
            // (transitioned by setMipLevelBarriers).
            // Destination: current mip level (m), written as a storage image. It is in GENERAL at dispatch time
            // (also transitioned by setMipLevelBarriers).
            DescriptorWriter()
                .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                       {depthPyramidSampler, depthPyramidImage->mipView(i, m - 1),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
                .image(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                       {VK_NULL_HANDLE, depthPyramidImage->mipView(i, m), VK_IMAGE_LAYOUT_GENERAL})
                .update(device, depthPyramidMipDescriptorSets[i][m]);
        }
    }
}

void RenderingResources::writeRCSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        // GBuffer images (position, normal, albedo, material), shared by the build and resolve sets.
        const std::array<VkDescriptorImageInfo, 4> gbInfos{{
            {gBuffer->getSampler(), gBuffer->getPositionView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getNormalView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getAlbedoView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getMaterialView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};

        // RC build: RC atlases as a storage image array
        std::vector<VkDescriptorImageInfo> radStorageInfos(RC_CASCADE_COUNT);
        for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
            radStorageInfos[c] = {VK_NULL_HANDLE, rcRadianceImages[c]->view(i), VK_IMAGE_LAYOUT_GENERAL};
        }

        DescriptorWriter buildWriter;
        buildWriter.buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo());
        for (uint32_t b = 0; b < 4; ++b) {
            buildWriter.image(1 + b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gbInfos[b]);
        }
        // Depth pyramid must use point sampling; the light pass result can use linear.
        buildWriter
            .image(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {depthPyramidSampler, depthPyramidImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, lightIncidentImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .images(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, radStorageInfos.data(), RC_CASCADE_COUNT)
            .update(device, rcBuildDescriptorSets[i]);

        // RC resolve: RC atlases as sampled arrays (lightPassSampler as the generic sampler). They stay in GENERAL
        // during build and resolve, so sample from GENERAL to avoid layout mismatches.
        std::vector<VkDescriptorImageInfo> radSampleInfos(RC_CASCADE_COUNT);
        for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
            radSampleInfos[c] = {lightPassSampler, rcRadianceImages[c]->view(i), VK_IMAGE_LAYOUT_GENERAL};
        }

        // GI history: frame i uses the previous frame's GI output for temporal accumulation, and the previous
        // frame's position and normal buffers for temporal validation.
        DescriptorWriter resolveWriter;
        resolveWriter.buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo());
        for (uint32_t b = 0; b < 4; ++b) {
            resolveWriter.image(1 + b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gbInfos[b]);
        }
        resolveWriter.images(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, radSampleInfos.data(), RC_CASCADE_COUNT)
            .image(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                   {VK_NULL_HANDLE, giIndirectImage->view(i), VK_IMAGE_LAYOUT_GENERAL}) // GI output
            .image(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, giIndirectImage->previousView(i), VK_IMAGE_LAYOUT_GENERAL})
            .image(
                9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {gBuffer->getSampler(), gBuffer->position().previousView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBuffer->getSampler(), gBuffer->normal().previousView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, rcResolveDescriptorSets[i]);
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

std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> RenderingResources::createFrameContexts() {
    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> contexts;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        FrameContext& ctx = contexts[i];

        // Core frame data
        ctx.frameIndex = i;
        ctx.commandBuffer = VK_NULL_HANDLE; // Will be set by Renderer
        ctx.extent = {0, 0};                // Will be set by Renderer
        ctx.frameTime = 0.0f;               // Will be set by Renderer

        // Descriptor sets
        ctx.cameraDescriptorSet = cameraDescriptorSets[i];
        ctx.modelsDescriptorSet = modelsDescriptorSets[i];
        ctx.gBufferDescriptorSet = gBufferDescriptorSets[i];
        ctx.lightArrayDescriptorSet = lightArrayDescriptorSets[i];
        ctx.cascadeSplitsDescriptorSet = cascadeSplitsDescriptorSets[i];
        ctx.sceneLightingDescriptorSet = sceneLightingDescriptorSets[i];
        ctx.lightMatrixDescriptorSet = lightMatrixDescriptorSets[i];
        ctx.shadowModelMatrixDescriptorSet = shadowModelMatrixDescriptorSets[i];
        ctx.shadowMapSamplerDescriptorSet = shadowMapSamplerSets[i];
        ctx.skyboxDescriptorSet = skyboxDescriptorSet; // Single set, not per frame
        ctx.transparencyModelDescriptorSet = transparencyModelMatrixDescriptorSets[i];
        ctx.compositionDescriptorSet = compositionDescriptorSets[i];
        ctx.depthPyramidDescriptorSet = depthPyramidDescriptorSets[i];
        ctx.rcBuildDescriptorSet = rcBuildDescriptorSets[i];
        ctx.rcResolveDescriptorSet = rcResolveDescriptorSets[i];
        ctx.smaaEdgeDescriptorSet = smaaEdgeDescriptorSets[i];
        ctx.smaaWeightDescriptorSet = smaaWeightDescriptorSets[i];
        ctx.smaaBlendDescriptorSet = smaaBlendDescriptorSets[i];
        ctx.colorCorrectionDescriptorSet = colorCorrectionDescriptorSets[i];

        // Buffers
        ctx.cameraUniformBuffer = cameraUniformBuffers[i].get();
        ctx.modelMatrixBuffer = modelMatrixBuffers[i].get();
        ctx.normalMatrixBuffer = normalMatrixBuffers[i].get();
        ctx.lightArrayUniformBuffer = lightArrayUniformBuffers[i].get();
        ctx.cascadeSplitsBuffer = cascadeSplitsBuffers[i].get();
        ctx.sceneLightingBuffer = sceneLightingBuffers[i].get();
        ctx.lightMatrixBuffer = lightMatrixBuffers[i].get();
        ctx.shadowModelMatrixBuffer = shadowModelMatrixBuffers[i].get();
        ctx.transparencyModelMatrixBuffer = transparencyModelMatrixBuffers[i].get();
        ctx.transparencyNormalMatrixBuffer = transparencyNormalMatrixBuffers[i].get();

        // Depth resources
        ctx.depthView = depthImage->view(i);
        ctx.depthImage = depthImage->image(i);
        // Depth pyramid
        ctx.depthPyramidView = depthPyramidImage->view(i);
        ctx.depthPyramidImage = depthPyramidImage->image(i);
        ctx.depthPyramidMipLevels = depthPyramidImage->mipLevels();
        ctx.depthPyramidMipStorageViews = depthPyramidImage->mipViews(i);
        ctx.depthPyramidMipDescriptorSets = depthPyramidMipDescriptorSets[i];

        // Light pass resources
        ctx.lightPassResultView = lightPassResultImage->view(i);
        ctx.lightPassSampler = lightPassSampler; // Single sampler, not per frame
        ctx.lightIncidentView = lightIncidentImage->view(i);

        // Transparency resources
        ctx.accumulationView = accumulationImage->view(i);
        ctx.revealageView = revealageImage->view(i);

        // GI indirect buffer
        ctx.giIndirectView = giIndirectImage->view(i);
        ctx.giIndirectImage = giIndirectImage->image(i);

        // Post-process render targets
        ctx.compositionColorView = compositionColorImage->view(i);
        ctx.compositionColorImage = compositionColorImage->image(i);
        ctx.smaaEdgeView = smaaEdgeImage->view(i);
        ctx.smaaEdgeImage = smaaEdgeImage->image(i);
        ctx.smaaBlendView = smaaBlendImage->view(i);
        ctx.smaaBlendImage = smaaBlendImage->image(i);
        ctx.postAAColorView = postAAColorImage->view(i);
        ctx.postAAColorImage = postAAColorImage->image(i);

        // GI history for temporal accumulation (previous frame's output)
        ctx.giHistoryView = giIndirectImage->previousView(i);
        ctx.giHistorySampler = lightPassSampler;

        // Initialize temporal frame index
        ctx.temporalFrameIndex = 0;

        // GBuffer resources
        ctx.gBufferPositionView = gBuffer->getPositionView(i);
        ctx.gBufferNormalView = gBuffer->getNormalView(i);
        ctx.gBufferAlbedoView = gBuffer->getAlbedoView(i);
        ctx.gBufferMaterialView = gBuffer->getMaterialView(i);
        ctx.gBufferPositionImage = gBuffer->getPositionImage(i);
        ctx.gBufferNormalImage = gBuffer->getNormalImage(i);
        ctx.gBufferAlbedoImage = gBuffer->getAlbedoImage(i);
        ctx.gbufferMaterialImage = gBuffer->getMaterialImage(i);

        // RC atlas views for this frame
        for (uint32_t cascade = 0; cascade < RC_CASCADE_COUNT; ++cascade) {
            ctx.rcRadianceViews[cascade] = rcRadianceImages[cascade]->view(i);
        }

        // Shadow map references - now frame-specific shadow maps
        for (size_t j = 0; j < MAX_DIRECTIONAL_LIGHTS; j++) {
            ctx.directionalShadowMaps[j] = directionalMaps[j][i].get(); // [lightIndex][frameIndex]
        }
        for (size_t j = 0; j < MAX_SPOT_LIGHTS; j++) {
            ctx.spotShadowMaps[j] = spotlightMaps[j][i].get(); // [lightIndex][frameIndex]
        }
        for (size_t j = 0; j < MAX_POINT_LIGHTS; j++) {
            ctx.pointShadowMaps[j] = pointlightMaps[j][i].get(); // [lightIndex][frameIndex]
        }

        // Material batches (will be populated by Renderer during updates)
        ctx.opaqueMaterialBatchCount = 0;
    }

    return contexts;
}

void RenderingResources::updateSkyboxDescriptorSet(VkImageView skyboxImageView, VkSampler skyboxSampler) {
    DescriptorWriter()
        .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
               {skyboxSampler, skyboxImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
        .update(device, skyboxDescriptorSet);
}

void RenderingResources::initializeSkyboxFromScene() {
    // Get the scene environment lighting
    const Scene::EnvironmentLighting& envLighting = Scene::Scene::getInstance().getEnvironmentLighting();

    if (envLighting.skyboxTexture != nullptr) {
        // Use the actual skybox cubemap from the scene
        updateSkyboxDescriptorSet(envLighting.skyboxTexture->getImageView(), envLighting.skyboxTexture->getSampler());
    } else {
        // Fallback to placeholder if no skybox texture is found
        Log::warn("No skybox texture found in scene, using placeholder");
        VkImageView placeholderView = gBuffer->getAlbedoView(0);
        VkSampler placeholderSampler = lightPassSampler;
        updateSkyboxDescriptorSet(placeholderView, placeholderSampler);
    }
}

} // namespace Rendering