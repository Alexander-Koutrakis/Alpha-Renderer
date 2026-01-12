#include "rendering_resources.hpp"
#include <stdexcept>
#include <iostream>
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
    
    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device.getDevice(), "vkSetDebugUtilsObjectNameEXT");
    if (func != nullptr) {
        func(device.getDevice(), &nameInfo);
    }
}

RenderingResources::RenderingResources(Device& device, SwapChain& swapChain)
    : device(device), swapChain(swapChain) {
    
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
    
    // TODO: Replace with proper skybox texture when implemented
    // For now, use a placeholder to avoid validation errors
    initializeSkyboxFromScene();

    std::cout << "RenderingResources created with " << width << "x" << height << std::endl;
}

RenderingResources::~RenderingResources() {
    cleanup();
}

void RenderingResources::findResourcesFormats() {
    // Initialize depth format
    depthFormat = device.getDepthFormat();
    
    // Unified HDR format for lighting + post-processing chain
    hdrFormat = device.findSupportedFormat(
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    );

    

    // Get formats from GBuffer to ensure consistency
    positionFormat = gBuffer->getPositionFormat();
    normalFormat = gBuffer->getNormalFormat();
    albedoFormat = gBuffer->getAlbedoFormat();
    materialFormat = gBuffer->getMaterialFormat();

    revealageFormat = device.findSupportedFormat(
        {VK_FORMAT_R8_UNORM, VK_FORMAT_R16_UNORM},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    );

    // Depth pyramid min/max float format (sampled + storage) — RG16/32
    depthPyramidFormat = device.findSupportedFormat(
        {VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32G32_SFLOAT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
    );

    // GI indirect buffer format (R16G16B16A16 preferred), must support color attachment, sampled, and storage
    giIndirectFormat = device.findSupportedFormat(
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
    );

    // Post-processing intermediates stay in HDR; tonemap later when writing to the swapchain
    postProcessFormat = hdrFormat;

    // SMAA intermediates: ensure the chosen formats support color attachment + sampling
    smaaEdgeFormat = device.findSupportedFormat(
        {VK_FORMAT_R8G8_UNORM},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    );
    smaaBlendFormat = device.findSupportedFormat(
        {VK_FORMAT_R8G8B8A8_UNORM},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    );

    std::cout << "RenderingResources formats found:" << std::endl;
    std::cout << "  Depth: " << depthFormat << std::endl;
    std::cout << "  Position: " << positionFormat << std::endl;
    std::cout << "  Normal: " << normalFormat << std::endl;
    std::cout << "  Albedo: " << albedoFormat << std::endl;
    std::cout << "  Material: " << materialFormat << std::endl;
    std::cout << "  Revealage: " << revealageFormat << std::endl;
    std::cout << "  GI Indirect: " << giIndirectFormat << std::endl;
    std::cout << "  Depth Pyramid: " << depthPyramidFormat << std::endl;
}

void RenderingResources::createDepthResources() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Create depth image
        device.createImageWithInfo(
            imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            depthImages[i],
            depthMemories[i]
        );

        // Create depth image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &depthViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth image view!");
        }

        // Set debug names
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)depthImages[i], "DepthImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)depthViews[i], "DepthView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)depthMemories[i], "DepthMemory_Frame" + std::to_string(i));
    }

    std::cout << "Depth resources created: " << width << "x" << height << " format=" << depthFormat << std::endl;
}

void RenderingResources::createDepthPyramidResources(){
    auto computeMipLevels = [&](uint32_t w, uint32_t h)->uint32_t {
        uint32_t maxDim = std::max(w, h);
        uint32_t levels = 1;
        while ((maxDim >>= 1) > 0) { ++levels; }
        return levels;
    };

    const uint32_t requested = RC_DEPTH_MIP_LEVELS;
    const uint32_t maxPossible = computeMipLevels(width, height);
    const uint32_t mipLevels = requested == 0 ? maxPossible : std::min(requested, maxPossible);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthPyramidFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_STORAGE_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        device.createImageWithInfo(
            imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            depthPyramidImages[i],
            depthPyramidMemories[i]
        );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthPyramidImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthPyramidFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &depthPyramidViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth pyramid image view!");
        }

        depthPyramidMipLevels[i] = mipLevels;

        // Create per-mip views for all mips (0..mipLevels-1); used for both storage and sampling
        depthPyramidMipStorageViews[i].resize(mipLevels, VK_NULL_HANDLE);
        for (uint32_t m = 0; m < mipLevels; ++m) {
            VkImageViewCreateInfo mipViewInfo{};
            mipViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            mipViewInfo.image = depthPyramidImages[i];
            mipViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            mipViewInfo.format = depthPyramidFormat;
            mipViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipViewInfo.subresourceRange.baseMipLevel = m;
            mipViewInfo.subresourceRange.levelCount = 1;
            mipViewInfo.subresourceRange.baseArrayLayer = 0;
            mipViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device.getDevice(), &mipViewInfo, nullptr, &depthPyramidMipStorageViews[i][m]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create depth pyramid per-mip storage image view");
            }

            setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)depthPyramidMipStorageViews[i][m], 
                        "DepthPyramidMipView_Frame" + std::to_string(i) + "_Mip" + std::to_string(m));
        }

        // Set debug names for main pyramid resources
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)depthPyramidImages[i], "DepthPyramidImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)depthPyramidViews[i], "DepthPyramidView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)depthPyramidMemories[i], "DepthPyramidMemory_Frame" + std::to_string(i));
    }

    // One-time init: transition all pyramid images (all mips) to READ_ONLY so passes can assume a known starting layout
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = depthPyramidImages[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = depthPyramidMipLevels[i];
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
    }
    device.endSingleTimeCommands(cmd);

    // Create a dedicated sampler for depth pyramid sampling.
    // IMPORTANT: depth comparisons must be done with point sampling to avoid mixing geometry depth
    // with far-plane ("sky") depth near edges, which produces banding/missing-hit artifacts.
    if (depthPyramidSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
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
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);

        if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &depthPyramidSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth pyramid sampler!");
        }
        setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)depthPyramidSampler, "DepthPyramidSampler");
    }

    std::cout << "Depth pyramid created with " << mipLevels << " mips at " << width << "x" << height << std::endl;
}

void RenderingResources::createLightPassResources(){

    std::cout << "Creating light pass resources" << std::endl;
     // Create a sampler for the light pass result
     VkSamplerCreateInfo samplerInfo{};
     samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
     samplerInfo.magFilter = VK_FILTER_LINEAR;
     samplerInfo.minFilter = VK_FILTER_LINEAR;
     samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
     samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
     samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
     samplerInfo.anisotropyEnable = VK_FALSE;
     samplerInfo.maxAnisotropy = 1.0f;
     samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
     samplerInfo.unnormalizedCoordinates = VK_FALSE;
     samplerInfo.compareEnable = VK_FALSE;
     samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
     samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
     samplerInfo.mipLodBias = 0.0f;
     samplerInfo.minLod = 0.0f;
     samplerInfo.maxLod = 0.0f;
 
     if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &lightPassSampler) != VK_SUCCESS) {
         throw std::runtime_error("failed to create light pass sampler!");
     }
 
     std::cout << "Light pass sampler created" << std::endl;
     // Create light pass render target images
     for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {

         VkImageCreateInfo imageInfo{};
         imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
         imageInfo.imageType = VK_IMAGE_TYPE_2D;
         imageInfo.extent.width = width;
         imageInfo.extent.height = height;
         imageInfo.extent.depth = 1;
         imageInfo.mipLevels = 1;
         imageInfo.arrayLayers = 1;
         imageInfo.format = hdrFormat;
         imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
         imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
         imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
         imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
         imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
 
         device.createImageWithInfo(
             imageInfo,
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
             lightPassResultImages[i],
             lightPassResultMemories[i]
         );

         // Create image view
         VkImageViewCreateInfo viewInfo{};
         viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
         viewInfo.image = lightPassResultImages[i];
         viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
         viewInfo.format = hdrFormat;
         viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         viewInfo.subresourceRange.baseMipLevel = 0;
         viewInfo.subresourceRange.levelCount = 1;
         viewInfo.subresourceRange.baseArrayLayer = 0;
         viewInfo.subresourceRange.layerCount = 1;
 
         if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &lightPassResultViews[i]) != VK_SUCCESS) {
             throw std::runtime_error("failed to create light pass image view!");
         }

         // Set debug names
         setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)lightPassResultImages[i], "LightPassImage_Frame" + std::to_string(i));
         setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)lightPassResultViews[i], "LightPassView_Frame" + std::to_string(i));
         setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)lightPassResultMemories[i], "LightPassMemory_Frame" + std::to_string(i));

         // Incident diffuse buffer (pre-albedo) – same format/usages
         device.createImageWithInfo(
             imageInfo,
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
             lightIncidentImages[i],
             lightIncidentMemories[i]
         );

         viewInfo.image = lightIncidentImages[i];
         if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &lightIncidentViews[i]) != VK_SUCCESS) {
             throw std::runtime_error("failed to create light incident image view!");
         }
         setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)lightIncidentImages[i], "LightIncidentImage_Frame" + std::to_string(i));
         setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)lightIncidentViews[i], "LightIncidentView_Frame" + std::to_string(i));
         setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)lightIncidentMemories[i], "LightIncidentMemory_Frame" + std::to_string(i));
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

    std::cout << "RenderingResources cleaned up completely" << std::endl;
}

void RenderingResources::createBuffers(){
    
    std::cout << "Creating camera, model, and normal matrix buffers..." << std::endl;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        cameraUniformBuffers[i] = std::make_unique<Buffer>(
            device,
            sizeof(CameraUbo),
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        cameraUniformBuffers[i]->map();

        modelMatrixBuffers[i] = std::make_unique<Buffer>(
                device,
                sizeof(glm::mat4),
                BASE_INSTANCED_RENDERABLES,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        modelMatrixBuffers[i]->map();
            
        // Create normal matrix buffer
        normalMatrixBuffers[i] = std::make_unique<Buffer>(
                device,
                sizeof(glm::mat4),
                BASE_INSTANCED_RENDERABLES,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        normalMatrixBuffers[i]->map();

        // Set debug names for buffers
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)cameraUniformBuffers[i]->getBuffer(), "CameraUniformBuffer_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)modelMatrixBuffers[i]->getBuffer(), "ModelMatrixBuffer_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)normalMatrixBuffers[i]->getBuffer(), "NormalMatrixBuffer_Frame" + std::to_string(i));
    }
    std::cout << "Camera, model, and normal matrix buffers created successfully." << std::endl;

    std::cout << "Creating light array uniform buffers..." << std::endl;
    VkDeviceSize unifiedLightBufferSize = sizeof(UnifiedLightBuffer); 
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        lightArrayUniformBuffers[i] = std::make_unique<Buffer>(
            device,
            unifiedLightBufferSize,
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        lightArrayUniformBuffers[i]->map();
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)lightArrayUniformBuffers[i]->getBuffer(), "LightArrayUniformBuffer_Frame" + std::to_string(i));
    }
    std::cout << "Light array uniform buffers created successfully." << std::endl;

    std::cout << "Creating cascade splits buffers..." << std::endl;
    // Add cascade splits buffer creation
    VkDeviceSize cascadeSplitsBufferSize = sizeof(DirectionalLightCascadesBuffer);  
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        cascadeSplitsBuffers[i] = std::make_unique<Buffer>(
            device,
            cascadeSplitsBufferSize,
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        cascadeSplitsBuffers[i]->map();
    }
    std::cout << "Cascade splits buffers created successfully." << std::endl;

    std::cout << "Creating scene lighting buffers..." << std::endl;
    VkDeviceSize sceneLightingBufferSize = sizeof(SceneLightingUbo);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        sceneLightingBuffers[i] = std::make_unique<Buffer>(
            device,
            sceneLightingBufferSize,
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        sceneLightingBuffers[i]->map();
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)sceneLightingBuffers[i]->getBuffer(), "SceneLightingBuffer_Frame" + std::to_string(i));
    }
    std::cout << "Scene lighting buffers created successfully." << std::endl;

    std::cout << "Creating light matrix buffers..." << std::endl;
    VkDeviceSize bufferSize = sizeof(ShadowcastingLightMatrices);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        lightMatrixBuffers[i] = std::make_unique<Buffer>(
            device,
            bufferSize,
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        lightMatrixBuffers[i]->map();
    }
    std::cout << "Light matrix buffers created successfully." << std::endl;

    std::cout << "Creating shadow model matrix buffers..." << std::endl;
    VkDeviceSize shadowModelMatrixBuffer = sizeof(glm::mat4);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        shadowModelMatrixBuffers[i] = std::make_unique<Buffer>(
            device,
            shadowModelMatrixBuffer,
            BASE_INSTANCED_RENDERABLES * MAX_SHADOW_CASCADE_COUNT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        shadowModelMatrixBuffers[i]->map();
    }
    std::cout << "Shadow model matrix buffers created successfully." << std::endl;

    std::cout << "Creating transparency buffers..." << std::endl;
    VkDeviceSize matrixBufferSize = sizeof(glm::mat4);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        transparencyModelMatrixBuffers[i] = std::make_unique<Buffer>(
            device,
            matrixBufferSize,
            BASE_INSTANCED_RENDERABLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        transparencyModelMatrixBuffers[i]->map();

        transparencyNormalMatrixBuffers[i] = std::make_unique<Buffer>(
            device,
            matrixBufferSize,
            BASE_INSTANCED_RENDERABLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        transparencyNormalMatrixBuffers[i]->map();
    }
    std::cout << "Transparency buffers created successfully." << std::endl;

}

void RenderingResources::createDescriptorPool(){
    std::cout << "Creating descriptor pool..." << std::endl;
    // Recompute descriptor pool sizes with current pipelines (including RC and depth pyramid)
    std::cout << "Calculating descriptor pool sizes..." << std::endl;
    uint32_t pyrMaxMips = 0;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        pyrMaxMips = std::max(pyrMaxMips, depthPyramidMipLevels[i]);
    }
    const uint32_t pyramidExtraSetsPerFrame = (pyrMaxMips > 0) ? (pyrMaxMips - 1) : 0; // exclude seed mip0

    // Sets per frame:
    // 18 core sets (models, camera, gbuffer, lights, shadows, transparency, composition,
    // depth pyramid seed, RC build, RC resolve, SMAA edge/weight/blend, color correction, shadow sampler)
    // + per-mip depth pyramid sets.
    const uint32_t totalDescriptorSets =
        MAX_FRAMES_IN_FLIGHT * (18 + pyramidExtraSetsPerFrame) +
        1; // skybox

    // Uniform buffers per frame: camera, light array, cascade splits, scene lighting, light matrix, RC build, RC resolve
    const uint32_t uniformBufferCount = MAX_FRAMES_IN_FLIGHT * 7;

    // Storage buffers per frame: models (2), shadow models (1), transparency models (2)
    const uint32_t storageBufferCount = MAX_FRAMES_IN_FLIGHT * 5;

    // Combined image samplers per frame:
    const uint32_t gbufferSamplers = MAX_FRAMES_IN_FLIGHT * 4;
    const uint32_t shadowSamplers = MAX_FRAMES_IN_FLIGHT * (MAX_DIRECTIONAL_LIGHTS + MAX_SPOT_LIGHTS + MAX_POINT_LIGHTS);
    const uint32_t compositionSamplers = MAX_FRAMES_IN_FLIGHT * 4;
    const uint32_t depthPyramidSamplers = MAX_FRAMES_IN_FLIGHT * (1 + pyramidExtraSetsPerFrame); // seed + per-mip
    const uint32_t rcBuildSamplers = MAX_FRAMES_IN_FLIGHT * 6; // gbuffer4 + depth + incident
    const uint32_t rcResolveSamplers = MAX_FRAMES_IN_FLIGHT * (RC_CASCADE_COUNT + 6); // gbuffer4 + radiance array + history + prev pos
    const uint32_t smaaSamplers = MAX_FRAMES_IN_FLIGHT * (1 + 3 + 2); // edge + weight + blend
    const uint32_t colorCorrectionSamplers = MAX_FRAMES_IN_FLIGHT * 1;
    const uint32_t skyboxSamplers = 1;
    const uint32_t combinedImageSamplerCount =
        gbufferSamplers +
        shadowSamplers +
        compositionSamplers +
        depthPyramidSamplers +
        rcBuildSamplers +
        rcResolveSamplers +
        smaaSamplers +
        colorCorrectionSamplers +
        skyboxSamplers;

    // Storage images per frame:
    // RC build radiance atlases (N), depth pyramid seed (1), per-mip outputs, RC resolve GI output (1)
    const uint32_t storageImageCount =
        MAX_FRAMES_IN_FLIGHT * (RC_CASCADE_COUNT + 2 + pyramidExtraSetsPerFrame); // +2 = depth seed + gi output

    std::cout << "Pool sizes: " << totalDescriptorSets << " sets, "
              << uniformBufferCount << " uniform buffers, "
              << storageBufferCount << " storage buffers, "
              << combinedImageSamplerCount << " combined image samplers, "
              << storageImageCount << " storage images" << std::endl;

    std::cout << "Building descriptor pool..." << std::endl;
    descriptorPool = DescriptorPool::Builder(device)
            .setMaxSets(totalDescriptorSets)
            .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBufferCount)
            .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferCount)
            .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storageImageCount)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, combinedImageSamplerCount)
            .build();
    std::cout << "Descriptor pool created successfully." << std::endl;
}

void RenderingResources::createDescriptorSetLayouts(){

    std::cout << "Creating models descriptor set layout..." << std::endl;
    // Create descriptor set layout for instance storage buffers
    std::array<VkDescriptorSetLayoutBinding, 2> instanceBindings{};     
    // Model matrix storage buffer
    instanceBindings[0].binding = 0;
    instanceBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    instanceBindings[0].descriptorCount = 1;
    instanceBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        
    // Normal matrix storage buffer
    instanceBindings[1].binding = 1;
    instanceBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    instanceBindings[1].descriptorCount = 1;
    instanceBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo instanceLayoutInfo{};
    instanceLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    instanceLayoutInfo.bindingCount = static_cast<uint32_t>(instanceBindings.size());
    instanceLayoutInfo.pBindings = instanceBindings.data();

    if (vkCreateDescriptorSetLayout(device.getDevice(), &instanceLayoutInfo, nullptr, &modelsDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create instance buffer descriptor set layout");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)modelsDescriptorSetLayout, "ModelsDescriptorSetLayout");
    std::cout << "Models descriptor set layout created successfully." << std::endl;


    std::cout << "Creating material descriptor set layout..." << std::endl;
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

        // Material UBO (binding = 1)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Albedo texture (binding = 2)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Normal map (binding = 3)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Metallic-smoothness map (binding = 4)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Occlusion map (binding = 5)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setMaterialLayoutInfo{};
    setMaterialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setMaterialLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    setMaterialLayoutInfo.pBindings = bindings.data();


    if (vkCreateDescriptorSetLayout(device.getDevice(), &setMaterialLayoutInfo, nullptr, &materialDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout for materials");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)materialDescriptorSetLayout, "MaterialDescriptorSetLayout");
    std::cout << "Material descriptor set layout created successfully." << std::endl;

    //Create descriptor set layout for camera uniform buffer
    std::cout << "Creating camera descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding cameraBufferBinding{};
    cameraBufferBinding.binding = 0;
    cameraBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBufferBinding.descriptorCount = 1;
    cameraBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo cameraBufferLayoutInfo{};
    cameraBufferLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cameraBufferLayoutInfo.bindingCount = 1;
    cameraBufferLayoutInfo.pBindings = &cameraBufferBinding;
        
    if (vkCreateDescriptorSetLayout(device.getDevice(), &cameraBufferLayoutInfo, nullptr, &cameraDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout for set 0!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)cameraDescriptorSetLayout, "CameraDescriptorSetLayout");
    std::cout << "Camera descriptor set layout created successfully." << std::endl;

    std::cout << "Creating GBuffer descriptor set layout..." << std::endl;
    std::array<VkDescriptorSetLayoutBinding, 4> gBufferBindings{};
    for (size_t i = 0; i < gBufferBindings.size(); i++) {
        gBufferBindings[i].binding = i;
        gBufferBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gBufferBindings[i].descriptorCount = 1;
        gBufferBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo gBufferLayoutInfo{};
    gBufferLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    gBufferLayoutInfo.bindingCount = static_cast<uint32_t>(gBufferBindings.size());
    gBufferLayoutInfo.pBindings = gBufferBindings.data();

    if (vkCreateDescriptorSetLayout(device.getDevice(), &gBufferLayoutInfo, nullptr, &gBufferDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create gbuffer descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)gBufferDescriptorSetLayout, "GBufferDescriptorSetLayout");
    std::cout << "GBuffer descriptor set layout created successfully." << std::endl;

    std::cout << "Creating light array descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding lightArrayBinding{};
    lightArrayBinding.binding = 0;
    lightArrayBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightArrayBinding.descriptorCount = 1;
    lightArrayBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo lightLayoutInfo{};
    lightLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightLayoutInfo.bindingCount = 1;
    lightLayoutInfo.pBindings = &lightArrayBinding;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &lightLayoutInfo, nullptr, &lightArrayDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create unified light descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)lightArrayDescriptorSetLayout, "LightArrayDescriptorSetLayout");
    std::cout << "Light array descriptor set layout created successfully." << std::endl;

    std::cout << "Creating cascade splits descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding cascadeSplitsBinding{};
    cascadeSplitsBinding.binding = 0;
    cascadeSplitsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cascadeSplitsBinding.descriptorCount = 1;
    cascadeSplitsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo cascadeSplitsLayoutInfo{};
    cascadeSplitsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cascadeSplitsLayoutInfo.bindingCount = 1;
    cascadeSplitsLayoutInfo.pBindings = &cascadeSplitsBinding;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &cascadeSplitsLayoutInfo, nullptr, &cascadeSplitsSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create cascade splits descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)cascadeSplitsSetLayout, "CascadeSplitsDescriptorSetLayout");
    std::cout << "Cascade splits descriptor set layout created successfully." << std::endl;

    std::cout << "Creating scene lighting descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding sceneLightingBinding{};
    sceneLightingBinding.binding = 0;
    sceneLightingBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneLightingBinding.descriptorCount = 1;
    sceneLightingBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo sceneLightingLayoutInfo{};
    sceneLightingLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sceneLightingLayoutInfo.bindingCount = 1;
    sceneLightingLayoutInfo.pBindings = &sceneLightingBinding;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &sceneLightingLayoutInfo, nullptr, &sceneLightingDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create scene lighting descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)sceneLightingDescriptorSetLayout, "SceneLightingDescriptorSetLayout");
    std::cout << "Scene lighting descriptor set layout created successfully." << std::endl;

    // Create descriptor set layout (shared between both pipelines)
    std::cout << "Creating shadow light matrix descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding shadowUboBinding{};
    shadowUboBinding.binding = 0;
    shadowUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowUboBinding.descriptorCount = 1;
    shadowUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo shadowUbolayoutInfo{};
    shadowUbolayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    shadowUbolayoutInfo.bindingCount = 1;
    shadowUbolayoutInfo.pBindings = &shadowUboBinding;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &shadowUbolayoutInfo, nullptr, &shadowcastinglightMatrixDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)shadowcastinglightMatrixDescriptorSetLayout, "ShadowLightMatrixDescriptorSetLayout");
    std::cout << "Shadow light matrix descriptor set layout created successfully." << std::endl;

    // Create descriptor set layout for shadow map samplers
    std::cout << "Creating shadow map sampler descriptor set layout..." << std::endl;
    std::array<VkDescriptorSetLayoutBinding, 3> shadowMapLayoutBindings{};
    // Directional shadow maps
    shadowMapLayoutBindings[0].binding = 0;
    shadowMapLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapLayoutBindings[0].descriptorCount = MAX_DIRECTIONAL_LIGHTS;
    shadowMapLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Spot shadow maps
    shadowMapLayoutBindings[1].binding = 1;
    shadowMapLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapLayoutBindings[1].descriptorCount = MAX_SPOT_LIGHTS;
    shadowMapLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Point shadow maps (cubemaps)
    shadowMapLayoutBindings[2].binding = 2;
    shadowMapLayoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapLayoutBindings[2].descriptorCount = MAX_POINT_LIGHTS;
    shadowMapLayoutBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(shadowMapLayoutBindings.size());
    layoutInfo.pBindings = shadowMapLayoutBindings.data();
    
    if (vkCreateDescriptorSetLayout(device.getDevice(), &layoutInfo, nullptr, &shadowMapSamplerLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map sampler descriptor set layout");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)shadowMapSamplerLayout, "ShadowMapSamplerDescriptorSetLayout");
    std::cout << "Shadow map sampler descriptor set layout created successfully." << std::endl;

    //Create descriptor set layout for shadow model matrix
    std::cout << "Creating shadow model matrix descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding shadowModelMatrixBinding{};
    shadowModelMatrixBinding.binding = 0;
    shadowModelMatrixBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowModelMatrixBinding.descriptorCount = 1;
    shadowModelMatrixBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    VkDescriptorSetLayoutCreateInfo shadowModelMatrixLayoutInfo{};
    shadowModelMatrixLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    shadowModelMatrixLayoutInfo.bindingCount = 1;
    shadowModelMatrixLayoutInfo.pBindings = &shadowModelMatrixBinding;
    
    if (vkCreateDescriptorSetLayout(device.getDevice(), &shadowModelMatrixLayoutInfo, nullptr, &shadowModelMatrixDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow model matrix descriptor set layout");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)shadowModelMatrixDescriptorSetLayout, "ShadowModelMatrixDescriptorSetLayout");
    std::cout << "Shadow model matrix descriptor set layout created successfully." << std::endl;

    //Create descriptor set layout for skybox
    std::cout << "Creating skybox descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding skyboxBinding{};
    skyboxBinding.binding = 0;
    skyboxBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    skyboxBinding.descriptorCount = 1;
    skyboxBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo skyboxLayoutInfo{};
    skyboxLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    skyboxLayoutInfo.bindingCount = 1;
    skyboxLayoutInfo.pBindings = &skyboxBinding;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &skyboxLayoutInfo, nullptr, &skyboxDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skybox descriptor set layout");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)skyboxDescriptorSetLayout, "SkyboxDescriptorSetLayout");
    std::cout << "Skybox descriptor set layout created successfully." << std::endl;

    //Create descriptor set layout for transparency model matrix
      std::cout << "Creating transparency model descriptor set layout..." << std::endl;
      std::array<VkDescriptorSetLayoutBinding, 2> transparencyInstanceBindings{};     
      // Model matrix storage buffer
      transparencyInstanceBindings[0].binding = 0;
      transparencyInstanceBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      transparencyInstanceBindings[0].descriptorCount = 1;
      transparencyInstanceBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
          
      // Normal matrix storage buffer
      transparencyInstanceBindings[1].binding = 1;
      transparencyInstanceBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      transparencyInstanceBindings[1].descriptorCount = 1;
      transparencyInstanceBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  
      VkDescriptorSetLayoutCreateInfo transparencyInstanceLayoutInfo{};
      transparencyInstanceLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      transparencyInstanceLayoutInfo.bindingCount = static_cast<uint32_t>(transparencyInstanceBindings.size());
      transparencyInstanceLayoutInfo.pBindings = transparencyInstanceBindings.data();
  
      if (vkCreateDescriptorSetLayout(device.getDevice(), &transparencyInstanceLayoutInfo, nullptr, &transparencyModelDescriptorSetLayout) != VK_SUCCESS) {
          throw std::runtime_error("Failed to create transparency instance buffer descriptor set layout");
      }
      setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)transparencyModelDescriptorSetLayout, "TransparencyModelDescriptorSetLayout");
      std::cout << "Transparency model descriptor set layout created successfully." << std::endl;

    // Create descriptor set layout for composition textures
    std::cout << "Creating composition descriptor set layout..." << std::endl;
    std::array<VkDescriptorSetLayoutBinding, 4> compositionBindings{};
    
    // Binding 0: Opaque render result (from LightPass)
    compositionBindings[0].binding = 0;
    compositionBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositionBindings[0].descriptorCount = 1;
    compositionBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 1: Transparency accumulation buffer
    compositionBindings[1].binding = 1;
    compositionBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositionBindings[1].descriptorCount = 1;
    compositionBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 2: Transparency revealage buffer
    compositionBindings[2].binding = 2;
    compositionBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositionBindings[2].descriptorCount = 1;
    compositionBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 3: Indirect GI buffer
    compositionBindings[3].binding = 3;
    compositionBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositionBindings[3].descriptorCount = 1;
    compositionBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo compositionLayoutInfo{};
    compositionLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    compositionLayoutInfo.bindingCount = static_cast<uint32_t>(compositionBindings.size());
    compositionLayoutInfo.pBindings = compositionBindings.data();

    if (vkCreateDescriptorSetLayout(device.getDevice(), &compositionLayoutInfo, nullptr, &compositionSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create composition descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)compositionSetLayout, "CompositionDescriptorSetLayout");
    std::cout << "Composition descriptor set layout created successfully." << std::endl;

    // SMAA edge descriptor set layout (compositionColor input)
    std::cout << "Creating SMAA edge descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding smaaEdgeBinding{};
    smaaEdgeBinding.binding = 0;
    smaaEdgeBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    smaaEdgeBinding.descriptorCount = 1;
    smaaEdgeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo smaaEdgeLayoutInfo{};
    smaaEdgeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    smaaEdgeLayoutInfo.bindingCount = 1;
    smaaEdgeLayoutInfo.pBindings = &smaaEdgeBinding;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &smaaEdgeLayoutInfo, nullptr, &smaaEdgeSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create SMAA edge descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaEdgeSetLayout, "SMAAEdgeDescriptorSetLayout");
    std::cout << "SMAA edge descriptor set layout created successfully." << std::endl;

    // SMAA weight descriptor set layout (edges + area/search LUT)
    std::cout << "Creating SMAA weight descriptor set layout..." << std::endl;
    std::array<VkDescriptorSetLayoutBinding, 3> smaaWeightBindings{};
    smaaWeightBindings[0].binding = 0;
    smaaWeightBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    smaaWeightBindings[0].descriptorCount = 1;
    smaaWeightBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    smaaWeightBindings[1].binding = 1;
    smaaWeightBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    smaaWeightBindings[1].descriptorCount = 1;
    smaaWeightBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    smaaWeightBindings[2].binding = 2;
    smaaWeightBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    smaaWeightBindings[2].descriptorCount = 1;
    smaaWeightBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo smaaWeightLayoutInfo{};
    smaaWeightLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    smaaWeightLayoutInfo.bindingCount = static_cast<uint32_t>(smaaWeightBindings.size());
    smaaWeightLayoutInfo.pBindings = smaaWeightBindings.data();

    if (vkCreateDescriptorSetLayout(device.getDevice(), &smaaWeightLayoutInfo, nullptr, &smaaWeightSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create SMAA weight descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaWeightSetLayout, "SMAAWeightDescriptorSetLayout");
    std::cout << "SMAA weight descriptor set layout created successfully." << std::endl;

    // SMAA blend descriptor set layout (compositionColor + blend weights)
    std::cout << "Creating SMAA blend descriptor set layout..." << std::endl;
    std::array<VkDescriptorSetLayoutBinding, 2> smaaBlendBindings{};
    smaaBlendBindings[0].binding = 0;
    smaaBlendBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    smaaBlendBindings[0].descriptorCount = 1;
    smaaBlendBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    smaaBlendBindings[1].binding = 1;
    smaaBlendBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    smaaBlendBindings[1].descriptorCount = 1;
    smaaBlendBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo smaaBlendLayoutInfo{};
    smaaBlendLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    smaaBlendLayoutInfo.bindingCount = static_cast<uint32_t>(smaaBlendBindings.size());
    smaaBlendLayoutInfo.pBindings = smaaBlendBindings.data();

    if (vkCreateDescriptorSetLayout(device.getDevice(), &smaaBlendLayoutInfo, nullptr, &smaaBlendSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create SMAA blend descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)smaaBlendSetLayout, "SMAABlendDescriptorSetLayout");
    std::cout << "SMAA blend descriptor set layout created successfully." << std::endl;

    // Color correction descriptor set layout (post-AA color input)
    std::cout << "Creating color correction descriptor set layout..." << std::endl;
    VkDescriptorSetLayoutBinding colorCorrectBinding{};
    colorCorrectBinding.binding = 0;
    colorCorrectBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colorCorrectBinding.descriptorCount = 1;
    colorCorrectBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo colorCorrectLayoutInfo{};
    colorCorrectLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    colorCorrectLayoutInfo.bindingCount = 1;
    colorCorrectLayoutInfo.pBindings = &colorCorrectBinding;

    if (vkCreateDescriptorSetLayout(device.getDevice(), &colorCorrectLayoutInfo, nullptr, &colorCorrectionSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create color correction descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)colorCorrectionSetLayout, "ColorCorrectionDescriptorSetLayout");
    std::cout << "Color correction descriptor set layout created successfully." << std::endl;

    // RC Build descriptor set layout
    std::cout << "Creating RC build descriptor set layout..." << std::endl;
    // NOTE: β is packed into uRadiance alpha (radiance.rgb, beta.a), so we only need one storage atlas array.
    std::array<VkDescriptorSetLayoutBinding, 8> rcBuildBindings{};
    // 0: Camera UBO
    rcBuildBindings[0].binding = 0;
    rcBuildBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    rcBuildBindings[0].descriptorCount = 1;
    rcBuildBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // 1..4: GBuffer
    for (uint32_t b = 1; b <= 4; ++b) {
        rcBuildBindings[b].binding = b;
        rcBuildBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        rcBuildBindings[b].descriptorCount = 1;
        rcBuildBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    // 5: Depth pyramid (sampled)
    rcBuildBindings[5].binding = 5;
    rcBuildBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rcBuildBindings[5].descriptorCount = 1;
    rcBuildBindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // 6: Light pass result (sampled)
    rcBuildBindings[6].binding = 6;
    rcBuildBindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rcBuildBindings[6].descriptorCount = 1;
    rcBuildBindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // 7: RC Radiance atlases (storage image array)
    rcBuildBindings[7].binding = 7;
    rcBuildBindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    rcBuildBindings[7].descriptorCount = RC_CASCADE_COUNT;
    rcBuildBindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo rcBuildLayoutInfo{};
    rcBuildLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    rcBuildLayoutInfo.bindingCount = static_cast<uint32_t>(rcBuildBindings.size());
    rcBuildLayoutInfo.pBindings = rcBuildBindings.data();

    if (vkCreateDescriptorSetLayout(device.getDevice(), &rcBuildLayoutInfo, nullptr, &rcBuildSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RC build descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)rcBuildSetLayout, "RCBuildDescriptorSetLayout");
    std::cout << "RC build descriptor set layout created successfully." << std::endl;

    // RC Resolve descriptor set layout
    std::cout << "Creating RC resolve descriptor set layout..." << std::endl;
    // NOTE: keep binding numbers stable (skip binding 6) to avoid shifting shader bindings:
    // binding 5 = radiance (rgba16f, beta in alpha), binding 7 = gi out, binding 8/9 = history/prev pos.
    std::array<VkDescriptorSetLayoutBinding, 9> rcResolveBindings{};
    // 0: Camera UBO
    rcResolveBindings[0].binding = 0;
    rcResolveBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    rcResolveBindings[0].descriptorCount = 1;
    rcResolveBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    // 1..4: GBuffer
    for (uint32_t b = 1; b <= 4; ++b) {
        rcResolveBindings[b].binding = b;
        rcResolveBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        rcResolveBindings[b].descriptorCount = 1;
        rcResolveBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    // 5: RC Radiance (sampled array)
    rcResolveBindings[5].binding = 5;
    rcResolveBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rcResolveBindings[5].descriptorCount = RC_CASCADE_COUNT;
    rcResolveBindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    // 7: GI output (storage image)
    rcResolveBindings[6].binding = 7;
    rcResolveBindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    rcResolveBindings[6].descriptorCount = 1;
    rcResolveBindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // 8: GI history (sampled image from previous frame for temporal accumulation)
    rcResolveBindings[7].binding = 8;
    rcResolveBindings[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rcResolveBindings[7].descriptorCount = 1;
    rcResolveBindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // 9: Previous frame position buffer (for temporal validation)
    rcResolveBindings[8].binding = 9;
    rcResolveBindings[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rcResolveBindings[8].descriptorCount = 1;
    rcResolveBindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo rcResolveLayoutInfo{};
    rcResolveLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    rcResolveLayoutInfo.bindingCount = static_cast<uint32_t>(rcResolveBindings.size());
    rcResolveLayoutInfo.pBindings = rcResolveBindings.data();

    if (vkCreateDescriptorSetLayout(device.getDevice(), &rcResolveLayoutInfo, nullptr, &rcResolveSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create RC resolve descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)rcResolveSetLayout, "RCResolveDescriptorSetLayout");
    std::cout << "RC resolve descriptor set layout created successfully." << std::endl;

    // Depth pyramid build descriptor set layout (centralized)
    std::cout << "Creating depth pyramid descriptor set layout..." << std::endl;
    std::array<VkDescriptorSetLayoutBinding, 2> pyrBindings{};
    // 0: source depth (combined image sampler)
    pyrBindings[0].binding = 0;
    pyrBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pyrBindings[0].descriptorCount = 1;
    pyrBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // 1: destination pyramid mip 0 (storage image)
    pyrBindings[1].binding = 1;
    pyrBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pyrBindings[1].descriptorCount = 1;
    pyrBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo pyrLayoutInfo{};
    pyrLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    pyrLayoutInfo.bindingCount = static_cast<uint32_t>(pyrBindings.size());
    pyrLayoutInfo.pBindings = pyrBindings.data();

    if (vkCreateDescriptorSetLayout(device.getDevice(), &pyrLayoutInfo, nullptr, &depthPyramidSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth pyramid descriptor set layout!");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)depthPyramidSetLayout, "DepthPyramidDescriptorSetLayout");
    std::cout << "Depth pyramid descriptor set layout created successfully." << std::endl;

}

void RenderingResources::createDescriptorSets(){

    std::cout << "Creating descriptor sets..." << std::endl;
     for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
        std::cout << "Creating descriptor sets for frame " << i << std::endl;
        
        //Create descriptor set for instance buffer
        std::cout << "  Creating models descriptor set..." << std::endl;
        VkDescriptorBufferInfo modelBufferInfo = modelMatrixBuffers[i]->descriptorInfo();
        VkDescriptorBufferInfo normalBufferInfo = normalMatrixBuffers[i]->descriptorInfo();     
        if (!DescriptorWriter(modelsDescriptorSetLayout, *descriptorPool)
            .writeBuffer(0, &modelBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, &normalBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .build(modelsDescriptorSets[i])) {
            throw std::runtime_error("Failed to create instance buffer descriptor set");
        }
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)modelsDescriptorSets[i], "ModelsDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  Models descriptor set created successfully." << std::endl;

        //Create descriptor set for camera buffer
        std::cout << "  Creating camera descriptor set..." << std::endl;
        VkDescriptorBufferInfo cameraBufferInfo = cameraUniformBuffers[i]->descriptorInfo();
        if (!DescriptorWriter(cameraDescriptorSetLayout, *descriptorPool)
            .writeBuffer(0, &cameraBufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .build(cameraDescriptorSets[i])) {
            throw std::runtime_error("Failed to create camera buffer descriptor set");
        }
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)cameraDescriptorSets[i], "CameraDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  Camera descriptor set created successfully." << std::endl;

       //Create descriptor set for gbuffer
        std::cout << "  Creating GBuffer descriptor set..." << std::endl;
        
        // First allocate the descriptor set
        VkDescriptorSetAllocateInfo allocInfoGBuffer{};
        allocInfoGBuffer.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoGBuffer.descriptorPool = descriptorPool->getDescriptorPool();
        allocInfoGBuffer.descriptorSetCount = 1;
        allocInfoGBuffer.pSetLayouts = &gBufferDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &allocInfoGBuffer, &gBufferDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate GBuffer descriptor set");
        }
        
        // Then prepare the image infos and update the descriptor set
        std::array<VkDescriptorImageInfo, 4> gbufferImageInfos;
        gbufferImageInfos[0] = {gBuffer->getSampler(), gBuffer->getPositionView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        gbufferImageInfos[1] = {gBuffer->getSampler(), gBuffer->getNormalView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        gbufferImageInfos[2] = {gBuffer->getSampler(), gBuffer->getAlbedoView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        gbufferImageInfos[3] = {gBuffer->getSampler(), gBuffer->getMaterialView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::array<VkWriteDescriptorSet, 4> gbufferDescriptorWrites{};
        for (size_t j = 0; j < gbufferDescriptorWrites.size(); j++) {
            gbufferDescriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gbufferDescriptorWrites[j].dstSet = gBufferDescriptorSets[i];
            gbufferDescriptorWrites[j].dstBinding = j;
            gbufferDescriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbufferDescriptorWrites[j].descriptorCount = 1;
            gbufferDescriptorWrites[j].pImageInfo = &gbufferImageInfos[j];
        }

        vkUpdateDescriptorSets(
            device.getDevice(),
            static_cast<uint32_t>(gbufferDescriptorWrites.size()),
            gbufferDescriptorWrites.data(),
            0, nullptr
        );
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)gBufferDescriptorSets[i], "GBufferDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  GBuffer descriptor set created successfully." << std::endl;

        //Create descriptor set for light array buffer
        std::cout << "  Creating light array descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo allocInfoUnifiedLight{};
        allocInfoUnifiedLight.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoUnifiedLight.descriptorPool = descriptorPool->getDescriptorPool();
        allocInfoUnifiedLight.descriptorSetCount = 1;
        allocInfoUnifiedLight.pSetLayouts = &lightArrayDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &allocInfoUnifiedLight, &lightArrayDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate light array buffer descriptor set");
        }
        VkDescriptorBufferInfo bufferInfoUnifiedLight{};
        bufferInfoUnifiedLight.buffer = lightArrayUniformBuffers[i]->getBuffer();
        bufferInfoUnifiedLight.offset = 0;
        bufferInfoUnifiedLight.range = lightArrayUniformBuffers[i]->getBufferSize();

        VkWriteDescriptorSet writeUnifiedLight{};
        writeUnifiedLight.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeUnifiedLight.dstSet = lightArrayDescriptorSets[i];
        writeUnifiedLight.dstBinding = 0;
        writeUnifiedLight.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeUnifiedLight.descriptorCount = 1;
        writeUnifiedLight.pBufferInfo = &bufferInfoUnifiedLight;

        vkUpdateDescriptorSets(device.getDevice(), 1, &writeUnifiedLight, 0, nullptr);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)lightArrayDescriptorSets[i], "LightArrayDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  Light array descriptor set created successfully." << std::endl;

        //Create descriptor set for cascade splits buffer
        std::cout << "  Creating cascade splits descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo allocInfoCascadeSplits{};
        allocInfoCascadeSplits.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoCascadeSplits.descriptorPool = descriptorPool->getDescriptorPool();
        allocInfoCascadeSplits.descriptorSetCount = 1;
        allocInfoCascadeSplits.pSetLayouts = &cascadeSplitsSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &allocInfoCascadeSplits, &cascadeSplitsDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate cascade splits buffer descriptor set");
        }

        VkDescriptorBufferInfo bufferInfoCascadeSplits{};
        bufferInfoCascadeSplits.buffer = cascadeSplitsBuffers[i]->getBuffer();
        bufferInfoCascadeSplits.offset = 0;
        bufferInfoCascadeSplits.range = cascadeSplitsBuffers[i]->getBufferSize();

        VkWriteDescriptorSet writeCascadeSplits{};
        writeCascadeSplits.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeCascadeSplits.dstSet = cascadeSplitsDescriptorSets[i];
        writeCascadeSplits.dstBinding = 0;
        writeCascadeSplits.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeCascadeSplits.descriptorCount = 1;
        writeCascadeSplits.pBufferInfo = &bufferInfoCascadeSplits;

        vkUpdateDescriptorSets(device.getDevice(), 1, &writeCascadeSplits, 0, nullptr);
        std::cout << "  Cascade splits descriptor set created successfully." << std::endl;


        //Create descriptor set for scene lighting buffer
        std::cout << "  Creating scene lighting descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo allocInfoSceneLightingUbo{};
        allocInfoSceneLightingUbo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoSceneLightingUbo.descriptorPool = descriptorPool->getDescriptorPool();
        allocInfoSceneLightingUbo.descriptorSetCount = 1;
        allocInfoSceneLightingUbo.pSetLayouts = &sceneLightingDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &allocInfoSceneLightingUbo, &sceneLightingDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate scene lighting descriptor set");
        }

        // Update with scene lighting buffer
        VkDescriptorBufferInfo bufferInfoSceneLightingUbo{};
        bufferInfoSceneLightingUbo.buffer = sceneLightingBuffers[i]->getBuffer();
        bufferInfoSceneLightingUbo.offset = 0;
        bufferInfoSceneLightingUbo.range = sceneLightingBuffers[i]->getBufferSize();

        VkWriteDescriptorSet writeSceneLightingUbo{};
        writeSceneLightingUbo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeSceneLightingUbo.dstSet = sceneLightingDescriptorSets[i];
        writeSceneLightingUbo.dstBinding = 0;
        writeSceneLightingUbo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeSceneLightingUbo.descriptorCount = 1;
        writeSceneLightingUbo.pBufferInfo = &bufferInfoSceneLightingUbo;

        vkUpdateDescriptorSets(device.getDevice(), 1, &writeSceneLightingUbo, 0, nullptr);
        std::cout << "  Scene lighting descriptor set created successfully." << std::endl;

        //Create descriptor set for light matrix
        std::cout << "  Creating light matrix descriptor set..." << std::endl;
        VkDescriptorBufferInfo bufferInfoLightMatrix{};
        bufferInfoLightMatrix.buffer = lightMatrixBuffers[i]->getBuffer();
        bufferInfoLightMatrix.offset = 0;
        bufferInfoLightMatrix.range = lightMatrixBuffers[i]->getBufferSize();

        DescriptorWriter(shadowcastinglightMatrixDescriptorSetLayout, *descriptorPool)
            .writeBuffer(0, &bufferInfoLightMatrix)
            .build(lightMatrixDescriptorSets[i]);
        std::cout << "  Light matrix descriptor set created successfully." << std::endl;

        //Create descriptor sets for show model matrices
        std::cout << "  Creating shadow model matrix descriptor set..." << std::endl;
        VkDescriptorBufferInfo bufferInfoModelMatrix{};
        bufferInfoModelMatrix.buffer = shadowModelMatrixBuffers[i]->getBuffer();
        bufferInfoModelMatrix.offset = 0;
        bufferInfoModelMatrix.range = shadowModelMatrixBuffers[i]->getBufferSize();

        DescriptorWriter(shadowModelMatrixDescriptorSetLayout, *descriptorPool)
            .writeBuffer(0, &bufferInfoModelMatrix, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .build(shadowModelMatrixDescriptorSets[i]);
        std::cout << "  Shadow model matrix descriptor set created successfully." << std::endl;

        //Create descriptor set for transparency model matrix
        std::cout << "  Creating transparency model matrix descriptor set..." << std::endl;
        VkDescriptorBufferInfo transparencyModelBufferInfo = transparencyModelMatrixBuffers[i]->descriptorInfo();
        VkDescriptorBufferInfo transparencyNormalBufferInfo = transparencyNormalMatrixBuffers[i]->descriptorInfo();     
        if (!DescriptorWriter(transparencyModelDescriptorSetLayout, *descriptorPool)
            .writeBuffer(0, &transparencyModelBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, &transparencyNormalBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .build(transparencyModelMatrixDescriptorSets[i])) {
            throw std::runtime_error("Failed to create transparency instance buffer descriptor set");
        }
        std::cout << "  Transparency model matrix descriptor set created successfully." << std::endl;

        std::cout << "  Creating composition descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool->getDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &compositionSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &allocInfo, &compositionDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate composition descriptor set");
        }

        // Prepare image infos
        std::array<VkDescriptorImageInfo, 4> compositionImageInfos;
        
        // Opaque render result from light pass
        compositionImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        compositionImageInfos[0].imageView = lightPassResultViews[i];
        compositionImageInfos[0].sampler = lightPassSampler;
        
        // Transparency accumulation buffer
        compositionImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        compositionImageInfos[1].imageView = accumulationViews[i];
        compositionImageInfos[1].sampler = lightPassSampler;
        
        // Transparency revealage buffer
        compositionImageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        compositionImageInfos[2].imageView = revealageViews[i];
        compositionImageInfos[2].sampler = lightPassSampler;

        // Indirect GI buffer stays in GENERAL because RCGI computes and readers share it within one frame.
        compositionImageInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        compositionImageInfos[3].imageView = giIndirectViews[i];
        compositionImageInfos[3].sampler = lightPassSampler;

        // Prepare write descriptor sets
        std::array<VkWriteDescriptorSet, 4> compositionDescriptorWrites{};
        for (size_t j = 0; j < compositionDescriptorWrites.size(); j++) {
            compositionDescriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            compositionDescriptorWrites[j].dstSet = compositionDescriptorSets[i];
            compositionDescriptorWrites[j].dstBinding = j;
            compositionDescriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            compositionDescriptorWrites[j].descriptorCount = 1;
            compositionDescriptorWrites[j].pImageInfo = &compositionImageInfos[j];
        }

        // Update descriptor sets
        vkUpdateDescriptorSets(
            device.getDevice(),
            static_cast<uint32_t>(compositionDescriptorWrites.size()),
            compositionDescriptorWrites.data(),
            0, nullptr
        );
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)compositionDescriptorSets[i], "CompositionDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  Composition descriptor set created successfully." << std::endl;

        // Create descriptor set for SMAA edge pass
        std::cout << "  Creating SMAA edge descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo smaaEdgeAlloc{};
        smaaEdgeAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        smaaEdgeAlloc.descriptorPool = descriptorPool->getDescriptorPool();
        smaaEdgeAlloc.descriptorSetCount = 1;
        smaaEdgeAlloc.pSetLayouts = &smaaEdgeSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &smaaEdgeAlloc, &smaaEdgeDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate SMAA edge descriptor set");
        }

        VkDescriptorImageInfo smaaEdgeInput{};
        smaaEdgeInput.sampler = postProcessSampler;
        smaaEdgeInput.imageView = compositionColorViews[i];
        smaaEdgeInput.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet smaaEdgeWrite{};
        smaaEdgeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        smaaEdgeWrite.dstSet = smaaEdgeDescriptorSets[i];
        smaaEdgeWrite.dstBinding = 0;
        smaaEdgeWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        smaaEdgeWrite.descriptorCount = 1;
        smaaEdgeWrite.pImageInfo = &smaaEdgeInput;

        vkUpdateDescriptorSets(device.getDevice(), 1, &smaaEdgeWrite, 0, nullptr);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)smaaEdgeDescriptorSets[i], "SMAAEdgeDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  SMAA edge descriptor set created successfully." << std::endl;

        // Create descriptor set for SMAA weight pass
        std::cout << "  Creating SMAA weight descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo smaaWeightAlloc{};
        smaaWeightAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        smaaWeightAlloc.descriptorPool = descriptorPool->getDescriptorPool();
        smaaWeightAlloc.descriptorSetCount = 1;
        smaaWeightAlloc.pSetLayouts = &smaaWeightSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &smaaWeightAlloc, &smaaWeightDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate SMAA weight descriptor set");
        }

        VkDescriptorImageInfo smaaEdgesInfo{};
        smaaEdgesInfo.sampler = postProcessSampler;
        smaaEdgesInfo.imageView = smaaEdgeViews[i];
        smaaEdgesInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo areaInfo{};
        areaInfo.sampler = smaaAreaSampler;
        areaInfo.imageView = smaaAreaView;
        areaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo searchInfo{};
        searchInfo.sampler = smaaSearchSampler;
        searchInfo.imageView = smaaSearchView;
        searchInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 3> smaaWeightWrites{};
        smaaWeightWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        smaaWeightWrites[0].dstSet = smaaWeightDescriptorSets[i];
        smaaWeightWrites[0].dstBinding = 0;
        smaaWeightWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        smaaWeightWrites[0].descriptorCount = 1;
        smaaWeightWrites[0].pImageInfo = &smaaEdgesInfo;

        smaaWeightWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        smaaWeightWrites[1].dstSet = smaaWeightDescriptorSets[i];
        smaaWeightWrites[1].dstBinding = 1;
        smaaWeightWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        smaaWeightWrites[1].descriptorCount = 1;
        smaaWeightWrites[1].pImageInfo = &areaInfo;

        smaaWeightWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        smaaWeightWrites[2].dstSet = smaaWeightDescriptorSets[i];
        smaaWeightWrites[2].dstBinding = 2;
        smaaWeightWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        smaaWeightWrites[2].descriptorCount = 1;
        smaaWeightWrites[2].pImageInfo = &searchInfo;

        vkUpdateDescriptorSets(
            device.getDevice(),
            static_cast<uint32_t>(smaaWeightWrites.size()),
            smaaWeightWrites.data(),
            0, nullptr
        );
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)smaaWeightDescriptorSets[i], "SMAAWeightDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  SMAA weight descriptor set created successfully." << std::endl;

        // Create descriptor set for SMAA blend pass
        std::cout << "  Creating SMAA blend descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo smaaBlendAlloc{};
        smaaBlendAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        smaaBlendAlloc.descriptorPool = descriptorPool->getDescriptorPool();
        smaaBlendAlloc.descriptorSetCount = 1;
        smaaBlendAlloc.pSetLayouts = &smaaBlendSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &smaaBlendAlloc, &smaaBlendDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate SMAA blend descriptor set");
        }

        VkDescriptorImageInfo blendColor{};
        blendColor.sampler = postProcessSampler;
        blendColor.imageView = compositionColorViews[i];
        blendColor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo blendWeights{};
        blendWeights.sampler = postProcessSampler;
        blendWeights.imageView = smaaBlendViews[i];
        blendWeights.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> smaaBlendWrites{};
        smaaBlendWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        smaaBlendWrites[0].dstSet = smaaBlendDescriptorSets[i];
        smaaBlendWrites[0].dstBinding = 0;
        smaaBlendWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        smaaBlendWrites[0].descriptorCount = 1;
        smaaBlendWrites[0].pImageInfo = &blendColor;

        smaaBlendWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        smaaBlendWrites[1].dstSet = smaaBlendDescriptorSets[i];
        smaaBlendWrites[1].dstBinding = 1;
        smaaBlendWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        smaaBlendWrites[1].descriptorCount = 1;
        smaaBlendWrites[1].pImageInfo = &blendWeights;

        vkUpdateDescriptorSets(
            device.getDevice(),
            static_cast<uint32_t>(smaaBlendWrites.size()),
            smaaBlendWrites.data(),
            0, nullptr
        );
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)smaaBlendDescriptorSets[i], "SMAABlendDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  SMAA blend descriptor set created successfully." << std::endl;

        // Create descriptor set for color correction pass
        std::cout << "  Creating color correction descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo colorCorrectAlloc{};
        colorCorrectAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        colorCorrectAlloc.descriptorPool = descriptorPool->getDescriptorPool();
        colorCorrectAlloc.descriptorSetCount = 1;
        colorCorrectAlloc.pSetLayouts = &colorCorrectionSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &colorCorrectAlloc, &colorCorrectionDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate color correction descriptor set");
        }

        VkDescriptorImageInfo postAAInfo{};
        postAAInfo.sampler = postProcessSampler;
        postAAInfo.imageView = postAAColorViews[i];
        postAAInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet colorCorrectWrite{};
        colorCorrectWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        colorCorrectWrite.dstSet = colorCorrectionDescriptorSets[i];
        colorCorrectWrite.dstBinding = 0;
        colorCorrectWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        colorCorrectWrite.descriptorCount = 1;
        colorCorrectWrite.pImageInfo = &postAAInfo;

        vkUpdateDescriptorSets(device.getDevice(), 1, &colorCorrectWrite, 0, nullptr);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)colorCorrectionDescriptorSets[i], "ColorCorrectionDescriptorSet_Frame" + std::to_string(i));
        std::cout << "  Color correction descriptor set created successfully." << std::endl;

        // Create descriptor set for depth pyramid build (src depth + dst pyramid mip0)
        std::cout << "  Creating depth pyramid descriptor set..." << std::endl;
        VkDescriptorSetAllocateInfo allocInfoPyr{};
        allocInfoPyr.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoPyr.descriptorPool = descriptorPool->getDescriptorPool();
        allocInfoPyr.descriptorSetCount = 1;
        allocInfoPyr.pSetLayouts = &depthPyramidSetLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &allocInfoPyr, &depthPyramidDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate depth pyramid descriptor set");
        }

        VkDescriptorImageInfo srcDepthInfo{};
        srcDepthInfo.sampler = depthPyramidSampler; // point sampling for depth
        srcDepthInfo.imageView = depthViews[i];
        srcDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo dstPyrInfo{};
        dstPyrInfo.sampler = VK_NULL_HANDLE;
        // write mip0 storage view for seed
        dstPyrInfo.imageView = depthPyramidMipStorageViews[i][0];
        dstPyrInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // will be transitioned before dispatch

        std::array<VkWriteDescriptorSet, 2> pyrWrites{};
        pyrWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        pyrWrites[0].dstSet = depthPyramidDescriptorSets[i];
        pyrWrites[0].dstBinding = 0;
        pyrWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pyrWrites[0].descriptorCount = 1;
        pyrWrites[0].pImageInfo = &srcDepthInfo;

        pyrWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        pyrWrites[1].dstSet = depthPyramidDescriptorSets[i];
        pyrWrites[1].dstBinding = 1;
        pyrWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pyrWrites[1].descriptorCount = 1;
        pyrWrites[1].pImageInfo = &dstPyrInfo;

        vkUpdateDescriptorSets(device.getDevice(), static_cast<uint32_t>(pyrWrites.size()), pyrWrites.data(), 0, nullptr);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)depthPyramidDescriptorSets[i], "DepthPyramidDescriptorSet_Frame" + std::to_string(i));

        // Allocate per-mip descriptor sets (mips 1..N-1) for the downsample loop
        depthPyramidMipDescriptorSets[i].resize(depthPyramidMipLevels[i]);
        for (uint32_t m = 1; m < depthPyramidMipLevels[i]; ++m) {
            VkDescriptorSetAllocateInfo allocInfoMip{};
            allocInfoMip.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfoMip.descriptorPool = descriptorPool->getDescriptorPool();
            allocInfoMip.descriptorSetCount = 1;
            allocInfoMip.pSetLayouts = &depthPyramidSetLayout;
            if (vkAllocateDescriptorSets(device.getDevice(), &allocInfoMip, &depthPyramidMipDescriptorSets[i][m]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate depth pyramid per-mip descriptor set");
            }
            setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)depthPyramidMipDescriptorSets[i][m], 
                        "DepthPyramidMipDescriptorSet_Frame" + std::to_string(i) + "_Mip" + std::to_string(m));
            
            // Write the descriptor set immediately after allocation to avoid validation errors
            // Source: previous mip level (m-1) as sampled input
            // Note: The source mip will be in SHADER_READ_ONLY_OPTIMAL at dispatch time (transitioned by setMipLevelBarriers)
            VkDescriptorImageInfo srcMipInfo{};
            srcMipInfo.sampler = depthPyramidSampler;
            srcMipInfo.imageView = depthPyramidMipStorageViews[i][m - 1];
            srcMipInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            
            // Destination: current mip level (m) as storage output
            // Note: The destination mip will be in GENERAL at dispatch time (transitioned by setMipLevelBarriers)
            VkDescriptorImageInfo dstMipInfo{};
            dstMipInfo.sampler = VK_NULL_HANDLE;
            dstMipInfo.imageView = depthPyramidMipStorageViews[i][m];
            dstMipInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            
            std::array<VkWriteDescriptorSet, 2> mipWrites{};
            mipWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mipWrites[0].dstSet = depthPyramidMipDescriptorSets[i][m];
            mipWrites[0].dstBinding = 0;
            mipWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            mipWrites[0].descriptorCount = 1;
            mipWrites[0].pImageInfo = &srcMipInfo;
            
            mipWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mipWrites[1].dstSet = depthPyramidMipDescriptorSets[i][m];
            mipWrites[1].dstBinding = 1;
            mipWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            mipWrites[1].descriptorCount = 1;
            mipWrites[1].pImageInfo = &dstMipInfo;
            
            vkUpdateDescriptorSets(device.getDevice(), static_cast<uint32_t>(mipWrites.size()), mipWrites.data(), 0, nullptr);
        }
        std::cout << "  Depth pyramid descriptor set created successfully." << std::endl;

        
        std::cout << "Descriptor sets for frame " << i << " completed successfully." << std::endl;

    }

    // RC build/resolve descriptor sets per frame
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        // RC Build
        VkDescriptorSetAllocateInfo allocRCBuild{};
        allocRCBuild.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocRCBuild.descriptorPool = descriptorPool->getDescriptorPool();
        allocRCBuild.descriptorSetCount = 1;
        allocRCBuild.pSetLayouts = &rcBuildSetLayout;
        if (vkAllocateDescriptorSets(device.getDevice(), &allocRCBuild, &rcBuildDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate RC build descriptor set");
        }

        // Camera UBO
        VkDescriptorBufferInfo camUbo = cameraUniformBuffers[i]->descriptorInfo();
        // GBuffer images
        std::array<VkDescriptorImageInfo, 4> gbInfos{};
        gbInfos[0] = { gBuffer->getSampler(), gBuffer->getPositionView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        gbInfos[1] = { gBuffer->getSampler(), gBuffer->getNormalView(i),   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        gbInfos[2] = { gBuffer->getSampler(), gBuffer->getAlbedoView(i),   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        gbInfos[3] = { gBuffer->getSampler(), gBuffer->getMaterialView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        // Depth pyramid must use point sampling; light pass can use linear.
        VkDescriptorImageInfo depthPyrInfo{ depthPyramidSampler, depthPyramidViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo lightPassInfo{ lightPassSampler, lightIncidentViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        // RC atlases as storage image arrays
        std::vector<VkDescriptorImageInfo> radStorageInfos(RC_CASCADE_COUNT);
        for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
            radStorageInfos[c] = { VK_NULL_HANDLE, rcRadianceViews[c][i], VK_IMAGE_LAYOUT_GENERAL };
        }

        std::vector<VkWriteDescriptorSet> writesBuild;
        writesBuild.reserve(1 + 4 + 1 + 1 + 2);

        VkWriteDescriptorSet w0{}; w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w0.dstSet = rcBuildDescriptorSets[i]; w0.dstBinding = 0; w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w0.descriptorCount = 1; w0.pBufferInfo = &camUbo; writesBuild.push_back(w0);
        for (uint32_t b = 0; b < 4; ++b) {
            VkWriteDescriptorSet w{}; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = rcBuildDescriptorSets[i]; w.dstBinding = 1 + b; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.descriptorCount = 1; w.pImageInfo = &gbInfos[b]; writesBuild.push_back(w);
        }
        VkWriteDescriptorSet w5{}; w5.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w5.dstSet = rcBuildDescriptorSets[i]; w5.dstBinding = 5; w5.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w5.descriptorCount = 1; w5.pImageInfo = &depthPyrInfo; writesBuild.push_back(w5);
        VkWriteDescriptorSet w6{}; w6.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w6.dstSet = rcBuildDescriptorSets[i]; w6.dstBinding = 6; w6.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w6.descriptorCount = 1; w6.pImageInfo = &lightPassInfo; writesBuild.push_back(w6);
        VkWriteDescriptorSet w7{}; w7.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w7.dstSet = rcBuildDescriptorSets[i]; w7.dstBinding = 7; w7.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w7.descriptorCount = RC_CASCADE_COUNT; w7.pImageInfo = radStorageInfos.data(); writesBuild.push_back(w7);

        vkUpdateDescriptorSets(device.getDevice(), static_cast<uint32_t>(writesBuild.size()), writesBuild.data(), 0, nullptr);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)rcBuildDescriptorSets[i], "RCBuildDescriptorSet_Frame" + std::to_string(i));

        // RC Resolve
        VkDescriptorSetAllocateInfo allocRCResolve{};
        allocRCResolve.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocRCResolve.descriptorPool = descriptorPool->getDescriptorPool();
        allocRCResolve.descriptorSetCount = 1;
        allocRCResolve.pSetLayouts = &rcResolveSetLayout;
        if (vkAllocateDescriptorSets(device.getDevice(), &allocRCResolve, &rcResolveDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate RC resolve descriptor set");
        }

        // Camera
        VkDescriptorBufferInfo camUboResolve = cameraUniformBuffers[i]->descriptorInfo();
        // GBuffer again
        std::array<VkDescriptorImageInfo, 4> gbInfosResolve = gbInfos;
        // RC atlases sampled arrays (use lightPassSampler as generic sampler)
        std::vector<VkDescriptorImageInfo> radSampleInfos(RC_CASCADE_COUNT);
        for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
            // Atlases are kept in GENERAL during build+resolve; sample from GENERAL to avoid layout mismatches.
            radSampleInfos[c] = { lightPassSampler, rcRadianceViews[c][i], VK_IMAGE_LAYOUT_GENERAL };
        }
        // Output GI storage image
        VkDescriptorImageInfo giOut{}; giOut.imageLayout = VK_IMAGE_LAYOUT_GENERAL; giOut.imageView = giIndirectViews[i]; giOut.sampler = VK_NULL_HANDLE;

        // GI history: use previous frame's GI output for temporal accumulation
        // Frame i uses frame (i-1+MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT as history
        uint32_t historyFrameIndex = (i + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
        VkDescriptorImageInfo giHistoryInfo{};
        giHistoryInfo.sampler = lightPassSampler;
        giHistoryInfo.imageView = giIndirectViews[historyFrameIndex];
        giHistoryInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // Previous frame position buffer for temporal validation
        VkDescriptorImageInfo prevPosInfo{};
        prevPosInfo.sampler = gBuffer->getSampler();
        prevPosInfo.imageView = gBuffer->getPositionView(historyFrameIndex);
        prevPosInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::vector<VkWriteDescriptorSet> writesResolve;
        writesResolve.reserve(1 + 4 + 2 + 1 + 1 + 1);
        VkWriteDescriptorSet r0{}; r0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; r0.dstSet = rcResolveDescriptorSets[i]; r0.dstBinding = 0; r0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; r0.descriptorCount = 1; r0.pBufferInfo = &camUboResolve; writesResolve.push_back(r0);
        for (uint32_t b = 0; b < 4; ++b) { VkWriteDescriptorSet r{}; r.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; r.dstSet = rcResolveDescriptorSets[i]; r.dstBinding = 1 + b; r.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; r.descriptorCount = 1; r.pImageInfo = &gbInfosResolve[b]; writesResolve.push_back(r);}        
        VkWriteDescriptorSet r5{}; r5.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; r5.dstSet = rcResolveDescriptorSets[i]; r5.dstBinding = 5; r5.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; r5.descriptorCount = RC_CASCADE_COUNT; r5.pImageInfo = radSampleInfos.data(); writesResolve.push_back(r5);
        VkWriteDescriptorSet r7{}; r7.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; r7.dstSet = rcResolveDescriptorSets[i]; r7.dstBinding = 7; r7.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; r7.descriptorCount = 1; r7.pImageInfo = &giOut; writesResolve.push_back(r7);
        VkWriteDescriptorSet r8{}; r8.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; r8.dstSet = rcResolveDescriptorSets[i]; r8.dstBinding = 8; r8.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; r8.descriptorCount = 1; r8.pImageInfo = &giHistoryInfo; writesResolve.push_back(r8);
        VkWriteDescriptorSet r9{}; r9.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; r9.dstSet = rcResolveDescriptorSets[i]; r9.dstBinding = 9; r9.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; r9.descriptorCount = 1; r9.pImageInfo = &prevPosInfo; writesResolve.push_back(r9);

        vkUpdateDescriptorSets(device.getDevice(), static_cast<uint32_t>(writesResolve.size()), writesResolve.data(), 0, nullptr);
        setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)rcResolveDescriptorSets[i], "RCResolveDescriptorSet_Frame" + std::to_string(i));
    }
    
    // Create skybox descriptor set (single set, not per frame)
    std::cout << "Creating skybox descriptor set..." << std::endl;
    // Note: We need a skybox texture to properly populate this, for now just allocate the set
    VkDescriptorSetAllocateInfo allocInfoSkybox{};
    allocInfoSkybox.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfoSkybox.descriptorPool = descriptorPool->getDescriptorPool();
    allocInfoSkybox.descriptorSetCount = 1;
    allocInfoSkybox.pSetLayouts = &skyboxDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device.getDevice(), &allocInfoSkybox, &skyboxDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate skybox descriptor set");
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)skyboxDescriptorSet, "SkyboxDescriptorSet");
    std::cout << "Skybox descriptor set allocated successfully (requires texture to populate)." << std::endl;
      
}

void RenderingResources::createShadowMapSamplerDescriptorSets(){
    
    // Allocate descriptor sets for each frame
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorSetAllocateInfo allocInfoShadowMapSampler{};
        allocInfoShadowMapSampler.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoShadowMapSampler.descriptorPool = descriptorPool->getDescriptorPool();
        allocInfoShadowMapSampler.descriptorSetCount = 1;
        allocInfoShadowMapSampler.pSetLayouts = &shadowMapSamplerLayout;

        if (vkAllocateDescriptorSets(device.getDevice(), &allocInfoShadowMapSampler, &shadowMapSamplerSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate shadow map sampler descriptor set");
        }
    }
    
    // Update descriptor sets for each frame with frame-specific shadow maps
    for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
        // Prepare image infos for directional lights - use frame-specific shadow maps
        std::vector<VkDescriptorImageInfo> directionalImageInfos;
        for (size_t lightIndex = 0; lightIndex < MAX_DIRECTIONAL_LIGHTS; lightIndex++) {
            if (directionalMaps[lightIndex][frameIndex]) {
                directionalImageInfos.push_back({
                    directionalMaps[lightIndex][frameIndex]->getSampler(),
                    directionalMaps[lightIndex][frameIndex]->getImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                });
            }
        }

        // Prepare image infos for spot lights - use frame-specific shadow maps
        std::vector<VkDescriptorImageInfo> spotImageInfos;
        for (size_t lightIndex = 0; lightIndex < MAX_SPOT_LIGHTS; lightIndex++) {
            if (spotlightMaps[lightIndex][frameIndex]) {
                spotImageInfos.push_back({
                    spotlightMaps[lightIndex][frameIndex]->getSampler(),
                    spotlightMaps[lightIndex][frameIndex]->getImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                });
            }
        }

        // Prepare image infos for point lights - use frame-specific shadow maps
        std::vector<VkDescriptorImageInfo> pointImageInfos;
        for (size_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; lightIndex++) {
            if (pointlightMaps[lightIndex][frameIndex]) {
                pointImageInfos.push_back({
                    pointlightMaps[lightIndex][frameIndex]->getSampler(),
                    pointlightMaps[lightIndex][frameIndex]->getImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                });
            }
        }

        // Write descriptor sets for this frame
        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
        
        // Directional lights
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = shadowMapSamplerSets[frameIndex];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = static_cast<uint32_t>(directionalImageInfos.size());
        descriptorWrites[0].pImageInfo = directionalImageInfos.data();

        // Spot lights
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = shadowMapSamplerSets[frameIndex];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = static_cast<uint32_t>(spotImageInfos.size());
        descriptorWrites[1].pImageInfo = spotImageInfos.data();

        // Point lights
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = shadowMapSamplerSets[frameIndex];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = static_cast<uint32_t>(pointImageInfos.size());
        descriptorWrites[2].pImageInfo = pointImageInfos.data();

        // Update the descriptor sets for this frame
        vkUpdateDescriptorSets(
            device.getDevice(),
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
    }

}

void RenderingResources::createShadowMapResources(){

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

void RenderingResources::createTransparencyResources(){
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {

        VkImageCreateInfo accumulationImageInfo{};
        accumulationImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        accumulationImageInfo.imageType = VK_IMAGE_TYPE_2D;
        accumulationImageInfo.extent.width = width;
        accumulationImageInfo.extent.height = height;
        accumulationImageInfo.extent.depth = 1;
        accumulationImageInfo.mipLevels = 1;
        accumulationImageInfo.arrayLayers = 1;
        accumulationImageInfo.format = hdrFormat;
        accumulationImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        accumulationImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        accumulationImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        accumulationImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        accumulationImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        device.createImageWithInfo(
            accumulationImageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            accumulationImages[i],
            accumulationMemories[i]
        );

        VkImageViewCreateInfo accumulationViewInfo{};
        accumulationViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        accumulationViewInfo.image = accumulationImages[i];
        accumulationViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        accumulationViewInfo.format = hdrFormat;
        accumulationViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        accumulationViewInfo.subresourceRange.baseMipLevel = 0;
        accumulationViewInfo.subresourceRange.levelCount = 1;
        accumulationViewInfo.subresourceRange.baseArrayLayer = 0;
        accumulationViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.getDevice(), &accumulationViewInfo, nullptr, &accumulationViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create accumulation image view!");
        }


        // Create revealage texture (R8)
        VkImageCreateInfo revealageImageInfo{};
        revealageImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        revealageImageInfo.imageType = VK_IMAGE_TYPE_2D;
        revealageImageInfo.extent.width = width;
        revealageImageInfo.extent.height = height;
        revealageImageInfo.extent.depth = 1;
        revealageImageInfo.mipLevels = 1;
        revealageImageInfo.arrayLayers = 1;
        revealageImageInfo.format = revealageFormat;
        revealageImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        revealageImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        revealageImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        revealageImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        revealageImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        device.createImageWithInfo(
            revealageImageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            revealageImages[i],
            revealageMemories[i]
        );

        VkImageViewCreateInfo revealageViewInfo{};
        revealageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        revealageViewInfo.image = revealageImages[i];
        revealageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        revealageViewInfo.format = revealageFormat;
        revealageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        revealageViewInfo.subresourceRange.baseMipLevel = 0;
        revealageViewInfo.subresourceRange.levelCount = 1;
        revealageViewInfo.subresourceRange.baseArrayLayer = 0;
        revealageViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.getDevice(), &revealageViewInfo, nullptr, &revealageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create revealage image view!");
        }

        // Set debug names for transparency resources
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)accumulationImages[i], "AccumulationImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)accumulationViews[i], "AccumulationView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)accumulationMemories[i], "AccumulationMemory_Frame" + std::to_string(i));
        
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)revealageImages[i], "RevealageImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)revealageViews[i], "RevealageView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)revealageMemories[i], "RevealageMemory_Frame" + std::to_string(i));
    }


}

void RenderingResources::createGIResources(){
    // Create per-frame GI indirect images and views
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = giIndirectFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_STORAGE_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        device.createImageWithInfo(
            imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            giIndirectImages[i],
            giIndirectMemories[i]
        );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = giIndirectImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = giIndirectFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &giIndirectViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create GI indirect image view!");
        }

        // Set debug names
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)giIndirectImages[i], "GIIndirectImage_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)giIndirectViews[i], "GIIndirectView_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)giIndirectMemories[i], "GIIndirectMemory_Frame" + std::to_string(i));
    }

    // One-time init: transition GI images to GENERAL for compute writes
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = giIndirectImages[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
    }
    device.endSingleTimeCommands(cmd);
}

void RenderingResources::createPostProcessResources() {
    // Shared sampler for post-process textures (linear clamp)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &postProcessSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create post-process sampler!");
    }
    setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)postProcessSampler, "PostProcessSampler");

    auto makeColorImage = [&](VkFormat format, VkImage& image, VkDeviceMemory& memory, VkImageView& view, const std::string& name) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        device.createImageWithInfo(
            imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            image,
            memory
        );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
            throw std::runtime_error("failed to create post-process image view: " + name);
        }

        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, name + "_Image");
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)view, name + "_View");
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)memory, name + "_Memory");
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        makeColorImage(postProcessFormat, compositionColorImages[i], compositionColorMemories[i], compositionColorViews[i],
                       "CompositionColor_Frame" + std::to_string(i));
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
    auto createSMAAImage = [&](uint32_t w, uint32_t h, VkFormat format, const void* data,
                               VkImage& outImage, VkDeviceMemory& outMemory, 
                               VkImageView& outView, const std::string& name) {
        VkDeviceSize imageSize = w * h * ((format == VK_FORMAT_R8G8_UNORM) ? 2 : 1);
        
        // Create staging buffer
        Buffer stagingBuffer{
            device,
            imageSize,
            1,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        stagingBuffer.map(imageSize);
        stagingBuffer.writeToBuffer(data, imageSize);
        
        // Create image with exactly 1 mip level
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = w;
        imageInfo.extent.height = h;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;  // Critical: no mipmaps for SMAA LUTs
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        
        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outImage, outMemory);
        
        // Transition to transfer destination
        VkCommandBuffer cmd = device.beginSingleTimeCommands();
        
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = outImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        
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
        
        vkCmdCopyBufferToImage(cmd, stagingBuffer.getBuffer(), outImage, 
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        
        // Transition to shader read optimal
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        device.endSingleTimeCommands(cmd);
        
        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = outImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &outView) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SMAA LUT image view");
        }
        
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)outImage, name + "_Image");
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)outView, name + "_View");
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)outMemory, name + "_Memory");
    };
    
    // Create Area texture (160x560, R8G8, linear filtering, clamp)
    createSMAAImage(AREATEX_WIDTH, AREATEX_HEIGHT, VK_FORMAT_R8G8_UNORM, areaTexBytes,
                    smaaAreaImage, smaaAreaMemory, smaaAreaView, "SMAA_Area");
    
    // Create sampler for Area texture: LINEAR filtering, CLAMP_TO_EDGE
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        
        if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &smaaAreaSampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SMAA Area sampler");
        }
        setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)smaaAreaSampler, "SMAA_Area_Sampler");
    }
    
    // Create Search texture (64x16, R8, POINT/NEAREST filtering, clamp)
    createSMAAImage(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, VK_FORMAT_R8_UNORM, searchTexBytes,
                    smaaSearchImage, smaaSearchMemory, smaaSearchView, "SMAA_Search");
    
    // Create sampler for Search texture: NEAREST filtering, CLAMP_TO_EDGE
    // The search texture must use point sampling for correct lookups
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;  // Critical: POINT sampling
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        
        if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &smaaSearchSampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SMAA Search sampler");
        }
        setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)smaaSearchSampler, "SMAA_Search_Sampler");
    }
    
    std::cout << "SMAA LUT textures loaded (Area: " << AREATEX_WIDTH << "x" << AREATEX_HEIGHT 
              << ", Search: " << SEARCHTEX_WIDTH << "x" << SEARCHTEX_HEIGHT << ")" << std::endl;
}

void RenderingResources::createRCAtlases(){
    // Allocate per-cascade atlases (radiance: R16G16B16A16; β packed into alpha)
    for (uint32_t cascade = 0; cascade < RC_CASCADE_COUNT; ++cascade) {
        const uint32_t stridePx = RC_PROBE_STRIDE0_PX << cascade;     // Δp_i = 2^i
        const uint32_t tileSize = RC_BASE_TILE_SIZE << cascade;        // tile_i = base * 2^i

        const uint32_t probesX = (width + stridePx - 1) / stridePx;
        const uint32_t probesY = (height + stridePx - 1) / stridePx;

        const uint32_t atlasWidth  = std::max(1u, probesX * tileSize);
        const uint32_t atlasHeight = std::max(1u, probesY * tileSize);

        for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            // Radiance atlas
            VkImageCreateInfo radInfo{};
            radInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            radInfo.imageType = VK_IMAGE_TYPE_2D;
            radInfo.extent.width = atlasWidth;
            radInfo.extent.height = atlasHeight;
            radInfo.extent.depth = 1;
            radInfo.mipLevels = 1;
            radInfo.arrayLayers = 1;
            radInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            radInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            radInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            radInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            radInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            radInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            device.createImageWithInfo(
                radInfo,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                rcRadianceImages[cascade][frame],
                rcRadianceMemories[cascade][frame]
            );

            VkImageViewCreateInfo radView{};
            radView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            radView.image = rcRadianceImages[cascade][frame];
            radView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            radView.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            radView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            radView.subresourceRange.baseMipLevel = 0;
            radView.subresourceRange.levelCount = 1;
            radView.subresourceRange.baseArrayLayer = 0;
            radView.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device.getDevice(), &radView, nullptr, &rcRadianceViews[cascade][frame]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create RC radiance atlas view");
            }

            // Set debug names
            std::string cascadeFrameStr = "Cascade" + std::to_string(cascade) + "_Frame" + std::to_string(frame);
            setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)rcRadianceImages[cascade][frame], "RCRadianceImage_" + cascadeFrameStr);
            setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)rcRadianceViews[cascade][frame], "RCRadianceView_" + cascadeFrameStr);
            setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)rcRadianceMemories[cascade][frame], "RCRadianceMemory_" + cascadeFrameStr);

            std::cout << "RC atlas c=" << cascade << " f=" << frame
                      << " size=" << atlasWidth << "x" << atlasHeight
                      << " stridePx=" << stridePx << " tile=" << tileSize << std::endl;
        }
    }

    // One-time init: transition all RC atlas images to GENERAL for compute writes
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (uint32_t cascade = 0; cascade < RC_CASCADE_COUNT; ++cascade) {
        for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = rcRadianceImages[cascade][frame];
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }
    }
    device.endSingleTimeCommands(cmd);
}

std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> RenderingResources::createFrameContexts(){
    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> contexts;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        FrameContext& ctx = contexts[i];
        
        // Core frame data
        ctx.frameIndex = i;
        ctx.commandBuffer = VK_NULL_HANDLE;  // Will be set by Renderer
        ctx.extent = {0, 0};                 // Will be set by Renderer
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
        ctx.skyboxDescriptorSet = skyboxDescriptorSet;  // Single set, not per frame
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
        ctx.lightPassSampler = lightPassSampler;  // Single sampler, not per frame
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
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = skyboxImageView;
    imageInfo.sampler = skyboxSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = skyboxDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device.getDevice(), 1, &descriptorWrite, 0, nullptr);
}

void RenderingResources::initializeSkyboxFromScene() {
    std::cout << "Initializing skybox from scene..." << std::endl;
    
    // Get the scene environment lighting
    const Scene::EnvironmentLighting& envLighting = Scene::Scene::getInstance().getEnvironmentLighting();
    
    if (envLighting.skyboxTexture != nullptr) {
        // Use the actual skybox cubemap from the scene
        updateSkyboxDescriptorSet(
            envLighting.skyboxTexture->getImageView(),
            envLighting.skyboxTexture->getSampler()
        );
        std::cout << "Skybox initialized from scene environment lighting." << std::endl;
    } else {
        // Fallback to placeholder if no skybox texture is found
        std::cout << "No skybox texture found in scene, using placeholder..." << std::endl;
        VkImageView placeholderView = gBuffer->getAlbedoView(0);
        VkSampler placeholderSampler = lightPassSampler;
        updateSkyboxDescriptorSet(placeholderView, placeholderSampler);
        std::cout << "Placeholder skybox created." << std::endl;
    }
}

} // namespace Rendering 