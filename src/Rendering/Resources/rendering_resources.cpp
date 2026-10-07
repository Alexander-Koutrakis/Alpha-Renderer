#include "rendering_resources.hpp"
#include "Engine/log.hpp"
#include "Rendering/Core/barriers.hpp"
#include "Rendering/Core/images.hpp"
#include "Rendering/Core/samplers.hpp"
#include <stdexcept>
#include <algorithm>
#include "Scene/scene.hpp"
#include "external/smaa_textures/AreaTex.h"
#include "external/smaa_textures/SearchTex.h"

namespace Rendering {

void RenderingResources::setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name) {
    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = handle;
    nameInfo.pObjectName = name.c_str();

    auto func =
        (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device.getDevice(), "vkSetDebugUtilsObjectNameEXT");
    if (func != nullptr) {
        func(device.getDevice(), &nameInfo);
    }
}

RenderingResources::RenderingResources(Device& device, SwapChain& swapChain) : device(device), swapChain(swapChain) {
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
    createDescriptorPool();
    createDescriptorSetLayouts();
    loadSMAALUTTextures();
    createShadowMapResources();
    createDescriptorSets();
    createShadowMapSamplerDescriptorSets();

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
    VkImageCreateInfo imageInfo = imageCreateInfo2D(
        width, height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Create depth image
        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImages[i], depthMemories[i]);

        // Create depth image view
        depthViews[i] = createImageView(device.getDevice(), depthImages[i], depthFormat, VK_IMAGE_VIEW_TYPE_2D,
                                        VK_IMAGE_ASPECT_DEPTH_BIT);

        // Set debug names
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)depthImages[i], "DepthImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)depthViews[i], "DepthView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)depthMemories[i], "DepthMemory_Frame" + std::to_string(i));
    }

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

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageCreateInfo imageInfo =
            imageCreateInfo2D(width, height, depthPyramidFormat,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              mipLevels);

        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthPyramidImages[i],
                                   depthPyramidMemories[i]);

        depthPyramidViews[i] = createImageView(device.getDevice(), depthPyramidImages[i], depthPyramidFormat,
                                               VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels);

        depthPyramidMipLevels[i] = mipLevels;

        // Create per-mip views for all mips (0..mipLevels-1); used for both storage and sampling
        depthPyramidMipStorageViews[i].resize(mipLevels, VK_NULL_HANDLE);
        for (uint32_t m = 0; m < mipLevels; ++m) {
            depthPyramidMipStorageViews[i][m] =
                createImageView(device.getDevice(), depthPyramidImages[i], depthPyramidFormat, VK_IMAGE_VIEW_TYPE_2D,
                                VK_IMAGE_ASPECT_COLOR_BIT, m);

            setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)depthPyramidMipStorageViews[i][m],
                         "DepthPyramidMipView_Frame" + std::to_string(i) + "_Mip" + std::to_string(m));
        }

        // Set debug names for main pyramid resources
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)depthPyramidImages[i],
                     "DepthPyramidImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)depthPyramidViews[i],
                     "DepthPyramidView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)depthPyramidMemories[i],
                     "DepthPyramidMemory_Frame" + std::to_string(i));
    }

    // One-time init: transition all pyramid images (all mips) to READ_ONLY so passes can assume a known starting layout
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         ImageBarrierDesc::undefinedToShaderRead(depthPyramidImages[i], VK_IMAGE_ASPECT_COLOR_BIT, 1,
                                                                 depthPyramidMipLevels[i]));
    }
    device.endSingleTimeCommands(cmd);

    // Create a dedicated sampler for depth pyramid sampling.
    // IMPORTANT: depth comparisons must be done with point sampling to avoid mixing geometry depth
    // with far-plane ("sky") depth near edges, which produces banding/missing-hit artifacts.
    if (depthPyramidSampler == VK_NULL_HANDLE) {
        depthPyramidSampler = createSampler(device.getDevice(), VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
                                            static_cast<float>(mipLevels));
        setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)depthPyramidSampler, "DepthPyramidSampler");
    }

    Log::debug("Depth pyramid created with ", mipLevels, " mips at ", width, "x", height);
}

void RenderingResources::createLightPassResources() {
    // Create a sampler for the light pass result
    lightPassSampler = createSampler(device.getDevice(), VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, 0.0f);

    // Create light pass render target images
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageCreateInfo imageInfo = imageCreateInfo2D(
            width, height, hdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, lightPassResultImages[i],
                                   lightPassResultMemories[i]);

        // Create image view
        lightPassResultViews[i] = createImageView(device.getDevice(), lightPassResultImages[i], hdrFormat);

        // Set debug names
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)lightPassResultImages[i],
                     "LightPassImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)lightPassResultViews[i],
                     "LightPassView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)lightPassResultMemories[i],
                     "LightPassMemory_Frame" + std::to_string(i));

        // Incident diffuse buffer (pre-albedo) – same format/usages
        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, lightIncidentImages[i],
                                   lightIncidentMemories[i]);

        lightIncidentViews[i] = createImageView(device.getDevice(), lightIncidentImages[i], hdrFormat);
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)lightIncidentImages[i],
                     "LightIncidentImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)lightIncidentViews[i],
                     "LightIncidentView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)lightIncidentMemories[i],
                     "LightIncidentMemory_Frame" + std::to_string(i));
    }

    // Set debug name for sampler
    setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)lightPassSampler, "LightPassSampler");
}

void RenderingResources::cleanup() {
    // Wait for device to be idle before cleanup
    vkDeviceWaitIdle(device.getDevice());

    // Clean up descriptor set layouts
    if (materialDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), materialDescriptorSetLayout, nullptr);
        materialDescriptorSetLayout = VK_NULL_HANDLE;
    }
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

    // Clean up depth resources
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (depthViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device.getDevice(), depthViews[i], nullptr);
            depthViews[i] = VK_NULL_HANDLE;
        }
        if (depthImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(device.getDevice(), depthImages[i], nullptr);
            depthImages[i] = VK_NULL_HANDLE;
        }
        if (depthMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device.getDevice(), depthMemories[i], nullptr);
            depthMemories[i] = VK_NULL_HANDLE;
        }
    }

    // Clean up depth pyramid resources
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Clean up per-mip storage views first
        for (auto& mipView : depthPyramidMipStorageViews[i]) {
            if (mipView != VK_NULL_HANDLE) {
                vkDestroyImageView(device.getDevice(), mipView, nullptr);
                mipView = VK_NULL_HANDLE;
            }
        }
        depthPyramidMipStorageViews[i].clear();

        if (depthPyramidViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device.getDevice(), depthPyramidViews[i], nullptr);
            depthPyramidViews[i] = VK_NULL_HANDLE;
        }
        if (depthPyramidImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(device.getDevice(), depthPyramidImages[i], nullptr);
            depthPyramidImages[i] = VK_NULL_HANDLE;
        }
        if (depthPyramidMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device.getDevice(), depthPyramidMemories[i], nullptr);
            depthPyramidMemories[i] = VK_NULL_HANDLE;
        }
    }

    // Clean up light pass resources
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (lightPassResultViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device.getDevice(), lightPassResultViews[i], nullptr);
            lightPassResultViews[i] = VK_NULL_HANDLE;
        }
        if (lightIncidentViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device.getDevice(), lightIncidentViews[i], nullptr);
            lightIncidentViews[i] = VK_NULL_HANDLE;
        }
        if (lightPassResultImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(device.getDevice(), lightPassResultImages[i], nullptr);
            lightPassResultImages[i] = VK_NULL_HANDLE;
        }
        if (lightIncidentImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(device.getDevice(), lightIncidentImages[i], nullptr);
            lightIncidentImages[i] = VK_NULL_HANDLE;
        }
        if (lightPassResultMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device.getDevice(), lightPassResultMemories[i], nullptr);
            lightPassResultMemories[i] = VK_NULL_HANDLE;
        }
        if (lightIncidentMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device.getDevice(), lightIncidentMemories[i], nullptr);
            lightIncidentMemories[i] = VK_NULL_HANDLE;
        }
    }

    // Clean up transparency resources
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (accumulationViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device.getDevice(), accumulationViews[i], nullptr);
            accumulationViews[i] = VK_NULL_HANDLE;
        }
        if (accumulationImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(device.getDevice(), accumulationImages[i], nullptr);
            accumulationImages[i] = VK_NULL_HANDLE;
        }
        if (accumulationMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device.getDevice(), accumulationMemories[i], nullptr);
            accumulationMemories[i] = VK_NULL_HANDLE;
        }

        if (revealageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device.getDevice(), revealageViews[i], nullptr);
            revealageViews[i] = VK_NULL_HANDLE;
        }
        if (revealageImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(device.getDevice(), revealageImages[i], nullptr);
            revealageImages[i] = VK_NULL_HANDLE;
        }
        if (revealageMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device.getDevice(), revealageMemories[i], nullptr);
            revealageMemories[i] = VK_NULL_HANDLE;
        }
    }

    // Clean up GI indirect resources
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (giIndirectViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device.getDevice(), giIndirectViews[i], nullptr);
            giIndirectViews[i] = VK_NULL_HANDLE;
        }
        if (giIndirectImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(device.getDevice(), giIndirectImages[i], nullptr);
            giIndirectImages[i] = VK_NULL_HANDLE;
        }
        if (giIndirectMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device.getDevice(), giIndirectMemories[i], nullptr);
            giIndirectMemories[i] = VK_NULL_HANDLE;
        }
    }

    // Clean up post-process render targets
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto destroyImageTriple = [&](VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device.getDevice(), view, nullptr);
                view = VK_NULL_HANDLE;
            }
            if (image != VK_NULL_HANDLE) {
                vkDestroyImage(device.getDevice(), image, nullptr);
                image = VK_NULL_HANDLE;
            }
            if (memory != VK_NULL_HANDLE) {
                vkFreeMemory(device.getDevice(), memory, nullptr);
                memory = VK_NULL_HANDLE;
            }
        };

        destroyImageTriple(compositionColorImages[i], compositionColorMemories[i], compositionColorViews[i]);
        destroyImageTriple(smaaEdgeImages[i], smaaEdgeMemories[i], smaaEdgeViews[i]);
        destroyImageTriple(smaaBlendImages[i], smaaBlendMemories[i], smaaBlendViews[i]);
        destroyImageTriple(postAAColorImages[i], postAAColorMemories[i], postAAColorViews[i]);
    }

    // Clean up RC atlases
    for (uint32_t cascade = 0; cascade < RC_CASCADE_COUNT; ++cascade) {
        for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            if (rcRadianceViews[cascade][frame] != VK_NULL_HANDLE) {
                vkDestroyImageView(device.getDevice(), rcRadianceViews[cascade][frame], nullptr);
                rcRadianceViews[cascade][frame] = VK_NULL_HANDLE;
            }
            if (rcRadianceImages[cascade][frame] != VK_NULL_HANDLE) {
                vkDestroyImage(device.getDevice(), rcRadianceImages[cascade][frame], nullptr);
                rcRadianceImages[cascade][frame] = VK_NULL_HANDLE;
            }
            if (rcRadianceMemories[cascade][frame] != VK_NULL_HANDLE) {
                vkFreeMemory(device.getDevice(), rcRadianceMemories[cascade][frame], nullptr);
                rcRadianceMemories[cascade][frame] = VK_NULL_HANDLE;
            }
        }
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
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)cameraUniformBuffers[i]->getBuffer(),
                     "CameraUniformBuffer_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)modelMatrixBuffers[i]->getBuffer(),
                     "ModelMatrixBuffer_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)normalMatrixBuffers[i]->getBuffer(),
                     "NormalMatrixBuffer_Frame" + std::to_string(i));
    }

    VkDeviceSize unifiedLightBufferSize = sizeof(UnifiedLightBuffer);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        lightArrayUniformBuffers[i] =
            std::make_unique<Buffer>(device, unifiedLightBufferSize, 1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        lightArrayUniformBuffers[i]->map();
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)lightArrayUniformBuffers[i]->getBuffer(),
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
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)sceneLightingBuffers[i]->getBuffer(),
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

void RenderingResources::createDescriptorPool() {
    // Recompute descriptor pool sizes with current pipelines (including RC and depth pyramid)
    uint32_t pyrMaxMips = 0;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        pyrMaxMips = std::max(pyrMaxMips, depthPyramidMipLevels[i]);
    }
    const uint32_t pyramidExtraSetsPerFrame = (pyrMaxMips > 0) ? (pyrMaxMips - 1) : 0; // exclude seed mip0

    // Sets per frame:
    // 18 core sets (models, camera, gbuffer, lights, shadows, transparency, composition,
    // depth pyramid seed, RC build, RC resolve, SMAA edge/weight/blend, color correction, shadow sampler)
    // + per-mip depth pyramid sets.
    const uint32_t totalDescriptorSets = MAX_FRAMES_IN_FLIGHT * (18 + pyramidExtraSetsPerFrame) + 1; // skybox

    // Uniform buffers per frame: camera, light array, cascade splits, scene lighting, light matrix, RC build, RC resolve
    const uint32_t uniformBufferCount = MAX_FRAMES_IN_FLIGHT * 7;

    // Storage buffers per frame: models (2), shadow models (1), transparency models (2)
    const uint32_t storageBufferCount = MAX_FRAMES_IN_FLIGHT * 5;

    // Combined image samplers per frame:
    const uint32_t gbufferSamplers = MAX_FRAMES_IN_FLIGHT * 4;
    const uint32_t shadowSamplers =
        MAX_FRAMES_IN_FLIGHT * (MAX_DIRECTIONAL_LIGHTS + MAX_SPOT_LIGHTS + MAX_POINT_LIGHTS);
    const uint32_t compositionSamplers = MAX_FRAMES_IN_FLIGHT * 4;
    const uint32_t depthPyramidSamplers = MAX_FRAMES_IN_FLIGHT * (1 + pyramidExtraSetsPerFrame); // seed + per-mip
    const uint32_t rcBuildSamplers = MAX_FRAMES_IN_FLIGHT * 6; // gbuffer4 + depth + incident
    const uint32_t rcResolveSamplers =
        MAX_FRAMES_IN_FLIGHT * (RC_CASCADE_COUNT + 6);                // gbuffer4 + radiance array + history + prev pos
    const uint32_t smaaSamplers = MAX_FRAMES_IN_FLIGHT * (1 + 3 + 2); // edge + weight + blend
    const uint32_t colorCorrectionSamplers = MAX_FRAMES_IN_FLIGHT * 1;
    const uint32_t skyboxSamplers = 1;
    const uint32_t combinedImageSamplerCount = gbufferSamplers + shadowSamplers + compositionSamplers +
                                               depthPyramidSamplers + rcBuildSamplers + rcResolveSamplers +
                                               smaaSamplers + colorCorrectionSamplers + skyboxSamplers;

    // Storage images per frame:
    // RC build radiance atlases (N), depth pyramid seed (1), per-mip outputs, RC resolve GI output (1)
    const uint32_t storageImageCount =
        MAX_FRAMES_IN_FLIGHT * (RC_CASCADE_COUNT + 2 + pyramidExtraSetsPerFrame); // +2 = depth seed + gi output

    Log::debug("Pool sizes: ", totalDescriptorSets, " sets, ", uniformBufferCount, " uniform buffers, ",
               storageBufferCount, " storage buffers, ", combinedImageSamplerCount, " combined image samplers, ",
               storageImageCount, " storage images");

    descriptorPool = DescriptorPool::Builder(device)
                         .setMaxSets(totalDescriptorSets)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBufferCount)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferCount)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storageImageCount)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, combinedImageSamplerCount)
                         .build();
}

void RenderingResources::createDescriptorSetLayouts() {
    // Create descriptor set layout for instance storage buffers
    modelsDescriptorSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)modelsDescriptorSetLayout,
                 "ModelsDescriptorSetLayout");

    materialDescriptorSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)materialDescriptorSetLayout,
                 "MaterialDescriptorSetLayout");

    //Create descriptor set layout for camera uniform buffer
    cameraDescriptorSetLayout =
        createDescriptorSetLayout(device, {
                                              layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
                                          });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)cameraDescriptorSetLayout,
                 "CameraDescriptorSetLayout");

    gBufferDescriptorSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)gBufferDescriptorSetLayout,
                 "GBufferDescriptorSetLayout");

    lightArrayDescriptorSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)lightArrayDescriptorSetLayout,
                 "LightArrayDescriptorSetLayout");

    cascadeSplitsSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)cascadeSplitsSetLayout,
                 "CascadeSplitsDescriptorSetLayout");

    sceneLightingDescriptorSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)sceneLightingDescriptorSetLayout,
                 "SceneLightingDescriptorSetLayout");

    // Create descriptor set layout (shared between both pipelines)
    shadowcastinglightMatrixDescriptorSetLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
        });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)shadowcastinglightMatrixDescriptorSetLayout,
                 "ShadowLightMatrixDescriptorSetLayout");

    // Create descriptor set layout for shadow map samplers
    shadowMapSamplerLayout = createDescriptorSetLayout(
        device,
        {
            layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                          MAX_DIRECTIONAL_LIGHTS),
            layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_SPOT_LIGHTS),
            layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_POINT_LIGHTS),
        });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)shadowMapSamplerLayout,
                 "ShadowMapSamplerDescriptorSetLayout");

    //Create descriptor set layout for shadow model matrix
    shadowModelMatrixDescriptorSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)shadowModelMatrixDescriptorSetLayout,
                 "ShadowModelMatrixDescriptorSetLayout");

    //Create descriptor set layout for skybox
    skyboxDescriptorSetLayout =
        createDescriptorSetLayout(device, {
                                              layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                            VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT),
                                          });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)skyboxDescriptorSetLayout,
                 "SkyboxDescriptorSetLayout");

    //Create descriptor set layout for transparency model matrix
    transparencyModelDescriptorSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)transparencyModelDescriptorSetLayout,
                 "TransparencyModelDescriptorSetLayout");

    // Create descriptor set layout for composition textures
    compositionSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)compositionSetLayout,
                 "CompositionDescriptorSetLayout");

    // SMAA edge descriptor set layout (compositionColor input)
    smaaEdgeSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaEdgeSetLayout, "SMAAEdgeDescriptorSetLayout");

    // SMAA weight descriptor set layout (edges + area/search LUT)
    smaaWeightSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaWeightSetLayout, "SMAAWeightDescriptorSetLayout");

    // SMAA blend descriptor set layout (compositionColor + blend weights)
    smaaBlendSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaBlendSetLayout, "SMAABlendDescriptorSetLayout");

    // Color correction descriptor set layout (post-AA color input)
    colorCorrectionSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)colorCorrectionSetLayout,
                 "ColorCorrectionDescriptorSetLayout");

    // RC Build descriptor set layout
    // NOTE: β is packed into uRadiance alpha (radiance.rgb, beta.a), so we only need one storage atlas array.
    rcBuildSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, RC_CASCADE_COUNT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)rcBuildSetLayout, "RCBuildDescriptorSetLayout");

    // RC Resolve descriptor set layout
    // NOTE: keep binding numbers stable (skip binding 6) to avoid shifting shader bindings:
    // binding 5 = radiance (rgba16f, beta in alpha), binding 7 = gi out, binding 8/9 = history/prev pos.
    rcResolveSetLayout = createDescriptorSetLayout(
        device, {
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
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)rcResolveSetLayout, "RCResolveDescriptorSetLayout");

    // Depth pyramid build descriptor set layout (centralized)
    depthPyramidSetLayout = createDescriptorSetLayout(
        device, {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT),
                });
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)depthPyramidSetLayout,
                 "DepthPyramidDescriptorSetLayout");
}

void RenderingResources::createDescriptorSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        //Create descriptor set for instance buffer
        modelsDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, modelsDescriptorSetLayout);
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, modelMatrixBuffers[i]->descriptorInfo())
            .buffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, normalMatrixBuffers[i]->descriptorInfo())
            .update(device, modelsDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)modelsDescriptorSets[i],
                     "ModelsDescriptorSet_Frame" + std::to_string(i));

        //Create descriptor set for camera buffer
        cameraDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, cameraDescriptorSetLayout);
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo())
            .update(device, cameraDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)cameraDescriptorSets[i],
                     "CameraDescriptorSet_Frame" + std::to_string(i));

        //Create descriptor set for gbuffer

        gBufferDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, gBufferDescriptorSetLayout);
        const VkSampler gBufferSampler = gBuffer->getSampler();
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
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)gBufferDescriptorSets[i],
                     "GBufferDescriptorSet_Frame" + std::to_string(i));

        //Create descriptor set for light array buffer
        lightArrayDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, lightArrayDescriptorSetLayout);
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    lightArrayUniformBuffers[i]->descriptorInfo(lightArrayUniformBuffers[i]->getBufferSize()))
            .update(device, lightArrayDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)lightArrayDescriptorSets[i],
                     "LightArrayDescriptorSet_Frame" + std::to_string(i));

        //Create descriptor set for cascade splits buffer
        cascadeSplitsDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, cascadeSplitsSetLayout);
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    cascadeSplitsBuffers[i]->descriptorInfo(cascadeSplitsBuffers[i]->getBufferSize()))
            .update(device, cascadeSplitsDescriptorSets[i]);

        //Create descriptor set for scene lighting buffer
        sceneLightingDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, sceneLightingDescriptorSetLayout);
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    sceneLightingBuffers[i]->descriptorInfo(sceneLightingBuffers[i]->getBufferSize()))
            .update(device, sceneLightingDescriptorSets[i]);

        //Create descriptor set for light matrix
        lightMatrixDescriptorSets[i] =
            allocateDescriptorSet(*descriptorPool, shadowcastinglightMatrixDescriptorSetLayout);
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    lightMatrixBuffers[i]->descriptorInfo(lightMatrixBuffers[i]->getBufferSize()))
            .update(device, lightMatrixDescriptorSets[i]);

        //Create descriptor sets for show model matrices
        shadowModelMatrixDescriptorSets[i] =
            allocateDescriptorSet(*descriptorPool, shadowModelMatrixDescriptorSetLayout);
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    shadowModelMatrixBuffers[i]->descriptorInfo(shadowModelMatrixBuffers[i]->getBufferSize()))
            .update(device, shadowModelMatrixDescriptorSets[i]);

        //Create descriptor set for transparency model matrix
        transparencyModelMatrixDescriptorSets[i] =
            allocateDescriptorSet(*descriptorPool, transparencyModelDescriptorSetLayout);
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, transparencyModelMatrixBuffers[i]->descriptorInfo())
            .buffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, transparencyNormalMatrixBuffers[i]->descriptorInfo())
            .update(device, transparencyModelMatrixDescriptorSets[i]);

        compositionDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, compositionSetLayout);
        // The indirect GI buffer stays in GENERAL because RCGI computes and readers share it within one frame.
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, lightPassResultViews[i],
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // opaque result of the light pass
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, accumulationViews[i],
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // transparency accumulation
            .image(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, revealageViews[i],
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // transparency revealage
            .image(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, giIndirectViews[i], VK_IMAGE_LAYOUT_GENERAL}) // indirect GI
            .update(device, compositionDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)compositionDescriptorSets[i],
                     "CompositionDescriptorSet_Frame" + std::to_string(i));

        // Create descriptor set for SMAA edge pass
        smaaEdgeDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, smaaEdgeSetLayout);
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, compositionColorViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaEdgeDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)smaaEdgeDescriptorSets[i],
                     "SMAAEdgeDescriptorSet_Frame" + std::to_string(i));

        // Create descriptor set for SMAA weight pass
        smaaWeightDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, smaaWeightSetLayout);
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, smaaEdgeViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {smaaAreaSampler, smaaAreaView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {smaaSearchSampler, smaaSearchView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaWeightDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)smaaWeightDescriptorSets[i],
                     "SMAAWeightDescriptorSet_Frame" + std::to_string(i));

        // Create descriptor set for SMAA blend pass
        smaaBlendDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, smaaBlendSetLayout);
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, compositionColorViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, smaaBlendViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaBlendDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)smaaBlendDescriptorSets[i],
                     "SMAABlendDescriptorSet_Frame" + std::to_string(i));

        // Create descriptor set for color correction pass
        colorCorrectionDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, colorCorrectionSetLayout);
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, postAAColorViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, colorCorrectionDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)colorCorrectionDescriptorSets[i],
                     "ColorCorrectionDescriptorSet_Frame" + std::to_string(i));

        // Create descriptor set for depth pyramid build (src depth + dst pyramid mip0)
        depthPyramidDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, depthPyramidSetLayout);
        // Source depth is point sampled; the destination is pyramid mip 0 as a storage image, transitioned
        // to GENERAL before the dispatch.
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {depthPyramidSampler, depthViews[i], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                   {VK_NULL_HANDLE, depthPyramidMipStorageViews[i][0], VK_IMAGE_LAYOUT_GENERAL})
            .update(device, depthPyramidDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)depthPyramidDescriptorSets[i],
                     "DepthPyramidDescriptorSet_Frame" + std::to_string(i));

        // Allocate per-mip descriptor sets (mips 1..N-1) for the downsample loop
        depthPyramidMipDescriptorSets[i].resize(depthPyramidMipLevels[i]);
        for (uint32_t m = 1; m < depthPyramidMipLevels[i]; ++m) {
            depthPyramidMipDescriptorSets[i][m] = allocateDescriptorSet(*descriptorPool, depthPyramidSetLayout);
            setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)depthPyramidMipDescriptorSets[i][m],
                         "DepthPyramidMipDescriptorSet_Frame" + std::to_string(i) + "_Mip" + std::to_string(m));

            // Source: previous mip level (m-1), sampled. It is in SHADER_READ_ONLY_OPTIMAL at dispatch time
            // (transitioned by setMipLevelBarriers).
            // Destination: current mip level (m), written as a storage image. It is in GENERAL at dispatch time
            // (also transitioned by setMipLevelBarriers).
            DescriptorWriter()
                .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                       {depthPyramidSampler, depthPyramidMipStorageViews[i][m - 1],
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
                .image(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                       {VK_NULL_HANDLE, depthPyramidMipStorageViews[i][m], VK_IMAGE_LAYOUT_GENERAL})
                .update(device, depthPyramidMipDescriptorSets[i][m]);
        }
    }

    // RC build/resolve descriptor sets per frame
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        // RC Build
        rcBuildDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, rcBuildSetLayout);

        // GBuffer images (position, normal, albedo, material), reused by the resolve set below.
        const std::array<VkDescriptorImageInfo, 4> gbInfos{{
            {gBuffer->getSampler(), gBuffer->getPositionView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getNormalView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getAlbedoView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getMaterialView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};
        // RC atlases as a storage image array
        std::vector<VkDescriptorImageInfo> radStorageInfos(RC_CASCADE_COUNT);
        for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
            radStorageInfos[c] = {VK_NULL_HANDLE, rcRadianceViews[c][i], VK_IMAGE_LAYOUT_GENERAL};
        }

        DescriptorWriter buildWriter;
        buildWriter.buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo());
        for (uint32_t b = 0; b < 4; ++b) {
            buildWriter.image(1 + b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gbInfos[b]);
        }
        // Depth pyramid must use point sampling; the light pass result can use linear.
        buildWriter
            .image(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {depthPyramidSampler, depthPyramidViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, lightIncidentViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .images(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, radStorageInfos.data(), RC_CASCADE_COUNT)
            .update(device, rcBuildDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)rcBuildDescriptorSets[i],
                     "RCBuildDescriptorSet_Frame" + std::to_string(i));

        // RC Resolve
        rcResolveDescriptorSets[i] = allocateDescriptorSet(*descriptorPool, rcResolveSetLayout);

        // RC atlases as sampled arrays (lightPassSampler as the generic sampler). They stay in GENERAL during
        // build and resolve, so sample from GENERAL to avoid layout mismatches.
        std::vector<VkDescriptorImageInfo> radSampleInfos(RC_CASCADE_COUNT);
        for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
            radSampleInfos[c] = {lightPassSampler, rcRadianceViews[c][i], VK_IMAGE_LAYOUT_GENERAL};
        }

        // GI history: frame i uses the previous frame's GI output for temporal accumulation, and the previous
        // frame's position buffer for temporal validation.
        const uint32_t historyFrameIndex = (i + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;

        DescriptorWriter resolveWriter;
        resolveWriter.buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo());
        for (uint32_t b = 0; b < 4; ++b) {
            resolveWriter.image(1 + b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gbInfos[b]);
        }
        resolveWriter.images(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, radSampleInfos.data(), RC_CASCADE_COUNT)
            .image(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                   {VK_NULL_HANDLE, giIndirectViews[i], VK_IMAGE_LAYOUT_GENERAL}) // GI output
            .image(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, giIndirectViews[historyFrameIndex], VK_IMAGE_LAYOUT_GENERAL})
            .image(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBuffer->getSampler(), gBuffer->getPositionView(historyFrameIndex),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, rcResolveDescriptorSets[i]);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)rcResolveDescriptorSets[i],
                     "RCResolveDescriptorSet_Frame" + std::to_string(i));
    }

    // Create skybox descriptor set (single set, not per frame)
    // Note: We need a skybox texture to properly populate this, for now just allocate the set
    skyboxDescriptorSet = allocateDescriptorSet(*descriptorPool, skyboxDescriptorSetLayout);
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)skyboxDescriptorSet, "SkyboxDescriptorSet");
}

void RenderingResources::createShadowMapSamplerDescriptorSets() {
    // Allocate descriptor sets for each frame
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        shadowMapSamplerSets[i] = allocateDescriptorSet(*descriptorPool, shadowMapSamplerLayout);
    }

    // Update descriptor sets for each frame with frame-specific shadow maps
    for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
        // Prepare image infos for directional lights - use frame-specific shadow maps
        std::vector<VkDescriptorImageInfo> directionalImageInfos;
        for (size_t lightIndex = 0; lightIndex < MAX_DIRECTIONAL_LIGHTS; lightIndex++) {
            if (directionalMaps[lightIndex][frameIndex]) {
                directionalImageInfos.push_back({directionalMaps[lightIndex][frameIndex]->getSampler(),
                                                 directionalMaps[lightIndex][frameIndex]->getImageView(),
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            }
        }

        // Prepare image infos for spot lights - use frame-specific shadow maps
        std::vector<VkDescriptorImageInfo> spotImageInfos;
        for (size_t lightIndex = 0; lightIndex < MAX_SPOT_LIGHTS; lightIndex++) {
            if (spotlightMaps[lightIndex][frameIndex]) {
                spotImageInfos.push_back({spotlightMaps[lightIndex][frameIndex]->getSampler(),
                                          spotlightMaps[lightIndex][frameIndex]->getImageView(),
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            }
        }

        // Prepare image infos for point lights - use frame-specific shadow maps
        std::vector<VkDescriptorImageInfo> pointImageInfos;
        for (size_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; lightIndex++) {
            if (pointlightMaps[lightIndex][frameIndex]) {
                pointImageInfos.push_back({pointlightMaps[lightIndex][frameIndex]->getSampler(),
                                           pointlightMaps[lightIndex][frameIndex]->getImageView(),
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            }
        }

        // One array write per light type; the array sizes are however many shadow maps exist this frame.
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
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageCreateInfo accumulationImageInfo = imageCreateInfo2D(
            width, height, hdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        device.createImageWithInfo(accumulationImageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, accumulationImages[i],
                                   accumulationMemories[i]);

        accumulationViews[i] = createImageView(device.getDevice(), accumulationImages[i], hdrFormat);

        // Create revealage texture (R8)
        VkImageCreateInfo revealageImageInfo = imageCreateInfo2D(
            width, height, revealageFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        device.createImageWithInfo(revealageImageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, revealageImages[i],
                                   revealageMemories[i]);

        revealageViews[i] = createImageView(device.getDevice(), revealageImages[i], revealageFormat);

        // Set debug names for transparency resources
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)accumulationImages[i],
                     "AccumulationImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)accumulationViews[i],
                     "AccumulationView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)accumulationMemories[i],
                     "AccumulationMemory_Frame" + std::to_string(i));

        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)revealageImages[i], "RevealageImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)revealageViews[i], "RevealageView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)revealageMemories[i],
                     "RevealageMemory_Frame" + std::to_string(i));
    }
}

void RenderingResources::createGIResources() {
    // Create per-frame GI indirect images and views
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageCreateInfo imageInfo = imageCreateInfo2D(width, height, giIndirectFormat,
                                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, giIndirectImages[i],
                                   giIndirectMemories[i]);

        giIndirectViews[i] = createImageView(device.getDevice(), giIndirectImages[i], giIndirectFormat);

        // Set debug names
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)giIndirectImages[i], "GIIndirectImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)giIndirectViews[i],
                     "GIIndirectView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)giIndirectMemories[i],
                     "GIIndirectMemory_Frame" + std::to_string(i));
    }

    // One-time init: transition GI images to GENERAL for compute writes
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         ImageBarrierDesc::undefinedToGeneralShaderWrite(giIndirectImages[i]));
    }
    device.endSingleTimeCommands(cmd);
}

void RenderingResources::createPostProcessResources() {
    // Shared sampler for post-process textures (linear clamp)
    postProcessSampler = createSampler(device.getDevice(), VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, 0.0f);
    setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)postProcessSampler, "PostProcessSampler");

    auto makeColorImage = [&](VkFormat format, VkImage& image, VkDeviceMemory& memory, VkImageView& view,
                              const std::string& name) {
        VkImageCreateInfo imageInfo =
            imageCreateInfo2D(width, height, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        view = createImageView(device.getDevice(), image, format);

        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, name + "_Image");
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)view, name + "_View");
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)memory, name + "_Memory");
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        makeColorImage(postProcessFormat, compositionColorImages[i], compositionColorMemories[i],
                       compositionColorViews[i], "CompositionColor_Frame" + std::to_string(i));
        makeColorImage(smaaEdgeFormat, smaaEdgeImages[i], smaaEdgeMemories[i], smaaEdgeViews[i],
                       "SMAAEdge_Frame" + std::to_string(i));
        makeColorImage(smaaBlendFormat, smaaBlendImages[i], smaaBlendMemories[i], smaaBlendViews[i],
                       "SMAABlend_Frame" + std::to_string(i));
        makeColorImage(postProcessFormat, postAAColorImages[i], postAAColorMemories[i], postAAColorViews[i],
                       "PostAAColor_Frame" + std::to_string(i));
    }
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

        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)outImage, name + "_Image");
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)outView, name + "_View");
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)outMemory, name + "_Memory");
    };

    // Create Area texture (160x560, R8G8, linear filtering, clamp)
    createSMAAImage(AREATEX_WIDTH, AREATEX_HEIGHT, VK_FORMAT_R8G8_UNORM, areaTexBytes, smaaAreaImage, smaaAreaMemory,
                    smaaAreaView, "SMAA_Area");

    // Create sampler for Area texture: LINEAR filtering, CLAMP_TO_EDGE
    {
        smaaAreaSampler =
            createSampler(device.getDevice(), VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
        setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)smaaAreaSampler, "SMAA_Area_Sampler");
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
        setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)smaaSearchSampler, "SMAA_Search_Sampler");
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

        const uint32_t atlasWidth = std::max(1u, probesX * tileSize);
        const uint32_t atlasHeight = std::max(1u, probesY * tileSize);

        for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            // Radiance atlas
            VkImageCreateInfo radInfo = imageCreateInfo2D(atlasWidth, atlasHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

            device.createImageWithInfo(radInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rcRadianceImages[cascade][frame],
                                       rcRadianceMemories[cascade][frame]);

            rcRadianceViews[cascade][frame] =
                createImageView(device.getDevice(), rcRadianceImages[cascade][frame], VK_FORMAT_R16G16B16A16_SFLOAT);

            // Set debug names
            std::string cascadeFrameStr = "Cascade" + std::to_string(cascade) + "_Frame" + std::to_string(frame);
            setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)rcRadianceImages[cascade][frame],
                         "RCRadianceImage_" + cascadeFrameStr);
            setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)rcRadianceViews[cascade][frame],
                         "RCRadianceView_" + cascadeFrameStr);
            setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)rcRadianceMemories[cascade][frame],
                         "RCRadianceMemory_" + cascadeFrameStr);

            Log::debug("RC atlas c=", cascade, " f=", frame, " size=", atlasWidth, "x", atlasHeight,
                       " stridePx=", stridePx, " tile=", tileSize);
        }
    }

    // One-time init: transition all RC atlas images to GENERAL for compute writes
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (uint32_t cascade = 0; cascade < RC_CASCADE_COUNT; ++cascade) {
        for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             ImageBarrierDesc::undefinedToGeneralShaderWrite(rcRadianceImages[cascade][frame]));
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
        ctx.depthView = depthViews[i];
        ctx.depthImage = depthImages[i];
        // Depth pyramid
        ctx.depthPyramidView = depthPyramidViews[i];
        ctx.depthPyramidImage = depthPyramidImages[i];
        ctx.depthPyramidMipLevels = depthPyramidMipLevels[i];
        ctx.depthPyramidMipStorageViews = depthPyramidMipStorageViews[i];
        ctx.depthPyramidMipDescriptorSets = depthPyramidMipDescriptorSets[i];

        // Light pass resources
        ctx.lightPassResultView = lightPassResultViews[i];
        ctx.lightPassSampler = lightPassSampler; // Single sampler, not per frame
        ctx.lightIncidentView = lightIncidentViews[i];

        // Transparency resources
        ctx.accumulationView = accumulationViews[i];
        ctx.revealageView = revealageViews[i];

        // GI indirect buffer
        ctx.giIndirectView = giIndirectViews[i];
        ctx.giIndirectImage = giIndirectImages[i];

        // Post-process render targets
        ctx.compositionColorView = compositionColorViews[i];
        ctx.compositionColorImage = compositionColorImages[i];
        ctx.smaaEdgeView = smaaEdgeViews[i];
        ctx.smaaEdgeImage = smaaEdgeImages[i];
        ctx.smaaBlendView = smaaBlendViews[i];
        ctx.smaaBlendImage = smaaBlendImages[i];
        ctx.postAAColorView = postAAColorViews[i];
        ctx.postAAColorImage = postAAColorImages[i];

        // GI history for temporal accumulation (previous frame's output)
        uint32_t historyIndex = (i + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
        ctx.giHistoryView = giIndirectViews[historyIndex];
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
            ctx.rcRadianceViews[cascade] = rcRadianceViews[cascade][i];
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