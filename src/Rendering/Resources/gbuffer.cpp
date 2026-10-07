#include "gbuffer.hpp"
#include "Rendering/Core/barriers.hpp"
#include "Rendering/Core/images.hpp"

namespace Rendering {

void GBuffer::setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name) {
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

GBuffer::GBuffer(Device& device, const CreateInfo& createInfo)
    : device{device}, width{createInfo.width}, height{createInfo.height} {
    findResourcesFormats();
    createAttachments();
    createSampler();
}

void GBuffer::findResourcesFormats() {
    positionFormat = device.findSupportedFormat(
        {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT}, VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    // Normal attachment format (optimized for normal storage)
    normalFormat = device.findSupportedFormat(
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    // Albedo attachment format (standard color)
    albedoFormat =
        device.findSupportedFormat({VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    // Material attachment format (PBR properties)
    materialFormat =
        device.findSupportedFormat({VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
}

GBuffer::~GBuffer() {
    cleanup();
}

void GBuffer::cleanup() {
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.getDevice(), sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    auto cleanupArray = [this](std::array<VkImage, MAX_FRAMES_IN_FLIGHT>& images,
                               std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT>& memories,
                               std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& views) {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (views[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device.getDevice(), views[i], nullptr);
            }
            if (images[i] != VK_NULL_HANDLE) {
                vkDestroyImage(device.getDevice(), images[i], nullptr);
            }
            if (memories[i] != VK_NULL_HANDLE) {
                vkFreeMemory(device.getDevice(), memories[i], nullptr);
            }
        }
    };

    cleanupArray(positionImages, positionMemories, positionViews);
    cleanupArray(normalImages, normalMemories, normalViews);
    cleanupArray(albedoImages, albedoMemories, albedoViews);
    cleanupArray(materialImages, materialMemories, materialViews);
    std::cout << "GBuffer cleaned up" << std::endl;
}

void GBuffer::createAttachments() {
    createAttachment(positionFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, positionImages,
                     positionMemories, positionViews, "GBuffer_Position");

    createAttachment(normalFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, normalImages,
                     normalMemories, normalViews, "GBuffer_Normal");

    createAttachment(albedoFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, albedoImages,
                     albedoMemories, albedoViews, "GBuffer_Albedo");

    createAttachment(materialFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, materialImages,
                     materialMemories, materialViews, "GBuffer_Material");

    // Initialize all GBuffer images to SHADER_READ_ONLY_OPTIMAL layout
    VkCommandBuffer cmd = device.beginSingleTimeCommands();

    auto transitionAttachment = [&](const std::array<VkImage, MAX_FRAMES_IN_FLIGHT>& images) {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             ImageBarrierDesc::undefinedToShaderRead(images[i], VK_IMAGE_ASPECT_COLOR_BIT));
        }
    };

    transitionAttachment(positionImages);
    transitionAttachment(normalImages);
    transitionAttachment(albedoImages);
    transitionAttachment(materialImages);

    device.endSingleTimeCommands(cmd);
}

void GBuffer::createAttachment(VkFormat format, VkImageUsageFlags usage,
                               std::array<VkImage, MAX_FRAMES_IN_FLIGHT>& images,
                               std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT>& memories,
                               std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& views, const std::string& name) {
    VkImageCreateInfo imageInfo = imageCreateInfo2D(width, height, format, usage);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, images[i], memories[i]);

        views[i] = createImageView(device.getDevice(), images[i], format);

        // Set debug names
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)images[i], name + "Image_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)views[i], name + "View_Frame" + std::to_string(i));
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)memories[i], name + "Memory_Frame" + std::to_string(i));
    }
}

void GBuffer::createSampler() {
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

    if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create g-buffer sampler!");
    }

    setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "GBufferSampler");
}

} // namespace Rendering