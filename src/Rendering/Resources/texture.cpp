#include "texture.hpp"
#include <stdexcept>
#include "Rendering/Core/buffer.hpp"
#include <cmath>
#include <iostream>
namespace Rendering {

void Texture::setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name) {
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

 Texture::Texture(
    Device& device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    const void* data,
    uint32_t faces,
    const std::string& debugName) : device{device}, imageFormat{format}, width{width}, height{height}, textureName{debugName} {
    
   
    // Calculate maximum number of mipmap levels
    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    VkDeviceSize imageSize;
    imageSize = width * height * getFormatSize(format) * faces;
    
    // Create staging buffer
    Buffer stagingBuffer{
        device,
        imageSize,
        1,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };

    stagingBuffer.map(imageSize);
    stagingBuffer.writeToBuffer(data,imageSize);
    
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;


    device.createImageWithInfo(
        imageInfo,
        properties,
        image,
        imageMemory
    );

    // Transition image layout for copy
    transitionImageLayout(
        image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    // Copy buffer to image
    device.copyBufferToImage(
        stagingBuffer.getBuffer(),
        image,
        width,
        height,
        1
    );
    generateMipmaps();
    createTextureImageView();
    createTextureSampler();
    
    // Set debug names
    if (!textureName.empty()) {
        setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, "TextureImage_" + textureName);
        setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)imageView, "TextureView_" + textureName);
        setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)imageMemory, "TextureMemory_" + textureName);
        setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "TextureSampler_" + textureName);
    }
}

Texture::Texture(
    Device& device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImage existingImage,
    VkImageView existingImageView,
    VkDeviceMemory existingImageMemory,
    uint32_t existingMipLevels,
    const std::string& debugName
    ) : device{device}, 
        image{existingImage},
        imageMemory{existingImageMemory},
        imageView{existingImageView},
        sampler{VK_NULL_HANDLE},
        imageFormat{format}, 
        mipLevels{existingMipLevels},
        width{width}, 
        height{height},
        channels{0},
        imageLayout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        textureName{debugName} {
        
        // Create only the sampler since we already have the image and view
        createTextureSampler();
        
        // Set debug names for existing resources
        if (!textureName.empty()) {
            setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, "TextureImage_" + textureName);
            setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)imageView, "TextureView_" + textureName);
            setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)imageMemory, "TextureMemory_" + textureName);
            setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "TextureSampler_" + textureName);
        }
    }

Texture::~Texture() {
    vkDestroySampler(device.getDevice(), sampler, nullptr);
    vkDestroyImageView(device.getDevice(), imageView, nullptr);
    vkDestroyImage(device.getDevice(), image, nullptr);
    vkFreeMemory(device.getDevice(), imageMemory, nullptr);
}

VkDescriptorImageInfo Texture::getDescriptorInfo() const {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;
    return imageInfo;
}

void Texture::createTextureImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image view!");
    }
}

void Texture::createTextureSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    
        // Use standard settings for regular textures
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = device.deviceProperties.limits.maxSamplerAnisotropy;   
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipLodBias = -0.5f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);

    if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
    
}

void Texture::transitionImageLayout(
    VkImage vkImage,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t layerCount) {
    
    VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vkImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && 
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && 
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    device.endSingleTimeCommands(commandBuffer);
}

void Texture::generateMipmaps() {
    // Check if image format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(
        device.getPhysicalDevice(), 
        imageFormat,
        &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & 
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("texture image format does not support linear blitting!");
    }

    VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = width;
    int32_t mipHeight = height;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        // Calculate next mip level size
        int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {nextMipWidth, nextMipHeight, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(
            commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        // Update mip dimensions for next iteration
        mipWidth = nextMipWidth;
        mipHeight = nextMipHeight;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    device.endSingleTimeCommands(commandBuffer);
}

std::unique_ptr<Texture> Texture::createCubemap(
    Device& device,
    uint32_t size,
    VkFormat format,
    const std::vector<const void*>& faceData)
{
    std::unique_ptr<Texture> texture(new Texture(device));
    texture->imageFormat = format;
    texture->width = size;
    texture->height = size;
    
    // Calculate max mip levels for cubemap
    texture->mipLevels = static_cast<uint32_t>(std::floor(std::log2(size))) + 1;
    // Calculate buffer sizes
    VkDeviceSize faceSize = size * size * getFormatSize(format);
    VkDeviceSize totalSize = faceSize * 6;
    
    // Create single staging buffer for all faces
    Buffer stagingBuffer{
        device,
        totalSize,
        1,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };

    // Copy all face data into staging buffer
    stagingBuffer.map();
    for (int i = 0; i < 6; i++) {
        stagingBuffer.writeToBuffer(
            faceData[i],
            faceSize,  // size per face
            i * faceSize  // offset for each face
        );
    }

    // Create image for cubemap
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {size, size, 1};
    imageInfo.mipLevels = texture->mipLevels;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    device.createImageWithInfo(
        imageInfo,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        texture->image,
        texture->imageMemory
    );

    // Transition image layout for copy
    texture->transitionImageLayout(
        texture->image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        6  // layerCount
    );

    // Copy buffer to image
    VkBufferImageCopy regions[6];
    for (uint32_t face = 0; face < 6; face++) {
        regions[face].bufferOffset = face * faceSize;
        regions[face].bufferRowLength = 0;
        regions[face].bufferImageHeight = 0;
        regions[face].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[face].imageSubresource.mipLevel = 0;
        regions[face].imageSubresource.baseArrayLayer = face;
        regions[face].imageSubresource.layerCount = 1;
        regions[face].imageOffset = {0, 0, 0};
        regions[face].imageExtent = {size, size, 1};
    }

    // Copy all faces at once
    VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();
    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer.getBuffer(),
        texture->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        6,
        regions
    );
    device.endSingleTimeCommands(commandBuffer);

    // For cubemaps, we need to generate mipmaps for each face
    // Check if image format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(
        device.getPhysicalDevice(), 
        format,
        &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & 
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        // If mipmap generation isn't supported, fall back to just one level
        texture->mipLevels = 1;
        
        // Transition to shader read layout
        texture->transitionImageLayout(
            texture->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            6  // layerCount
        );
    } else {
        
        // Generate mipmaps for each face
        commandBuffer = device.beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = texture->image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        for (uint32_t face = 0; face < 6; face++) {
            // Set the layer for this face
            barrier.subresourceRange.baseArrayLayer = face;
            
            int32_t mipWidth = size;
            int32_t mipHeight = size;

            for (uint32_t i = 1; i < texture->mipLevels; i++) {
                barrier.subresourceRange.baseMipLevel = i - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier);

                // Calculate next mip level size
                int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
                int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = face;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {nextMipWidth, nextMipHeight, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = face;
                blit.dstSubresource.layerCount = 1;

                vkCmdBlitImage(
                    commandBuffer,
                    texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &blit,
                    VK_FILTER_LINEAR);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier);

                // Update mip dimensions for next iteration
                mipWidth = nextMipWidth;
                mipHeight = nextMipHeight;
            }

            // Transition the last mip level for this face
            barrier.subresourceRange.baseMipLevel = texture->mipLevels - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        }

        device.endSingleTimeCommands(commandBuffer);

        // After generating mipmaps, we need to record that all mip levels are now in SHADER_READ_ONLY_OPTIMAL layout
        texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // Create cubemap view and sampler
    texture->createCubemapView();
    texture->createCubemapSampler();
    
    // Set debug names for cubemap
    texture->textureName = "Cubemap";
    texture->setDebugName(VK_OBJECT_TYPE_IMAGE, (uint64_t)texture->image, "CubemapImage");
    texture->setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)texture->imageView, "CubemapView");
    texture->setDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)texture->imageMemory, "CubemapMemory");
    texture->setDebugName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)texture->sampler, "CubemapSampler");

    return texture;
}

void Texture::createCubemapView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;  // Cubemap view type
    viewInfo.format = imageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;  // 6 faces

    if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create cubemap image view!");
    }
}
void Texture::createCubemapSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    
    // Linear filtering for magnification and minification
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    
    // Important for cubemaps: use clamp to edge to prevent sampling beyond face edges
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    // Enable anisotropic filtering for better quality at angles
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = device.deviceProperties.limits.maxSamplerAnisotropy;
    
    // Border color isn't used with clamp to edge, but set it just in case
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    
    // Use normalized coordinates
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    
    // No comparison operations
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    
    // Mipmap settings
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);

    if (vkCreateSampler(device.getDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create cubemap sampler!");
    }
}

uint32_t Texture::getFormatSize(VkFormat format) {
    // For compressed formats, we return the size in bytes per block
    // BC formats use 4x4 pixel blocks
    switch (format) {
        // Uncompressed formats
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;  // 16 bits * 4 channels = 64 bits = 8 bytes
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16; // 32 bits * 4 channels = 128 bits = 16 bytes
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return 4;  // 32 bits packed = 4 bytes
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
            return 4;  // 8 bits * 4 channels = 32 bits = 4 bytes
            
        // BC1 formats (DXT1) - 8 bytes per 4x4 block (1/2 byte per pixel)
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return 8 / 16;  // 8 bytes per 16 pixels = 0.5 bytes per pixel
            
        // BC2 formats (DXT3) - 16 bytes per 4x4 block (1 byte per pixel)
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
            return 16 / 16;  // 16 bytes per 16 pixels = 1 byte per pixel
            
        // BC3 formats (DXT5) - 16 bytes per 4x4 block (1 byte per pixel)
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return 16 / 16;  // 16 bytes per 16 pixels = 1 byte per pixel
            
        // BC4 formats - 8 bytes per 4x4 block (1/2 byte per pixel)
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 8 / 16;  // 8 bytes per 16 pixels = 0.5 bytes per pixel
            
        // BC5 formats - 16 bytes per 4x4 block (1 byte per pixel)
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
            return 16 / 16;  // 16 bytes per 16 pixels = 1 byte per pixel
            
        // BC6H formats - 16 bytes per 4x4 block (1 byte per pixel)
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            return 16 / 16;  // 16 bytes per 16 pixels = 1 byte per pixel
            
        // BC7 formats - 16 bytes per 4x4 block (1 byte per pixel)
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return 16 / 16;  // 16 bytes per 16 pixels = 1 byte per pixel
            
        // ETC2/EAC formats (mobile)
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
            return 8 / 16;  // 8 bytes per 16 pixels = 0.5 bytes per pixel
            
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
            return 16 / 16;  // 16 bytes per 16 pixels = 1 byte per pixel
            
        // ASTC formats (mobile, variable block size)
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
            return 16 / 16;  // 16 bytes per 16 pixels = 1 byte per pixel
            
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
            return 16 / 64;  // 16 bytes per 64 pixels = 0.25 bytes per pixel
            
        default:
            return 4;  // Default to 4 bytes (8-bit RGBA)
    }
}

bool Texture::isBlockCompressedFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

   


}