#include "material.hpp"
#include <cassert>
#include <stdexcept>
#include <mutex>
#include <iostream>
namespace Rendering {

// Initialize static members
std::unique_ptr<Texture> Material::s_defaultTexture = nullptr;
std::once_flag Material::s_defaultTextureInitFlag;

void Material::setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name) {
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


Material::Material(
    Device& device,
    const MaterialInfo& materialInfo,
    DescriptorPool& descriptorPool,
    VkDescriptorSetLayout materialSetLayout)
    : device{device},
      info{materialInfo},
      properties{materialInfo.properties},
      descriptorPool{descriptorPool},
      materialSetLayout{materialSetLayout},
      transparencyType{materialInfo.transparencyType} {
    
        size_t uboSize = sizeof(MaterialUbo);

        propertiesBuffer = std::make_unique<Buffer>(
            device,
            uboSize,
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        propertiesBuffer->map();
        propertiesBuffer->writeToBuffer(&properties);
        
        // Set debug name for the material properties buffer
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)propertiesBuffer->getBuffer(), "MaterialPropertiesBuffer_" + info.name);
        
        createMaterialDescriptorSet();
        updateDescriptorSet();
}

Material::~Material() = default;

void Material::createMaterialDescriptorSet() {
    if (!DescriptorWriter(materialSetLayout, descriptorPool)
            .build(materialDescriptorSet)) {
        std::cerr << "Failed to allocate descriptor set for material: " << info.name << std::endl;
        std::cerr << "This might indicate that the descriptor pool is exhausted." << std::endl;
        throw std::runtime_error("failed to allocate material descriptor set for material '" + info.name + "'");
    }
    
    // Set debug name for the material descriptor set
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)materialDescriptorSet, "MaterialDescriptorSet_" + info.name);
}

void Material::updateDescriptorSet() {
    // Get buffer info for material UBO
    auto bufferInfo = propertiesBuffer->descriptorInfo();

    // Create a default white texture if we haven't yet (using static class member)
    std::call_once(s_defaultTextureInitFlag, [this]() {
        // Create a 1x1 white texture
        const uint32_t whitePixel = 0xFFFFFFFF;
        s_defaultTexture = std::make_unique<Texture>(
            device,
            1, 1,  // width, height
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &whitePixel,
            1,  // faces
            "DefaultWhiteTexture"
        );
    });

    // Use textures if they exist, otherwise use default
    auto albedoInfo = albedoTexture ? 
        albedoTexture->getDescriptorInfo() : 
        s_defaultTexture->getDescriptorInfo();
    
    auto normalInfo = normalTexture ? 
        normalTexture->getDescriptorInfo() : 
        s_defaultTexture->getDescriptorInfo();
    
    auto metallicSmoothnessInfo = metallicSmoothnessTexture ? 
        metallicSmoothnessTexture->getDescriptorInfo() : 
        s_defaultTexture->getDescriptorInfo();

    auto occlusionInfo = occlusionTexture ? 
        occlusionTexture->getDescriptorInfo() : 
        s_defaultTexture->getDescriptorInfo();

    // Write all descriptors
    DescriptorWriter(materialSetLayout, descriptorPool)
        .writeBuffer(0, &bufferInfo)
        .writeImage(1, &albedoInfo)
        .writeImage(2, &normalInfo)
        .writeImage(3, &metallicSmoothnessInfo)
        .writeImage(4, &occlusionInfo)
        .overwrite(materialDescriptorSet);
}

void Material::setAlbedoTexture(Texture* texture) {
    albedoTexture = texture;
    properties.hasAlbedoMap = texture ? 1 : 0;
    propertiesBuffer->writeToBuffer(&properties);
    updateDescriptorSet();
}

void Material::setNormalTexture(Texture* texture) {
    normalTexture = texture;
    properties.hasNormalMap = texture ? 1 : 0;
    propertiesBuffer->writeToBuffer(&properties);
    updateDescriptorSet();
}

void Material::setMetallicSmoothnessTexture(Texture* texture) {
    metallicSmoothnessTexture = texture;
    properties.hasMetallicSmoothnessMap = texture ? 1 : 0;
    propertiesBuffer->writeToBuffer(&properties);
    updateDescriptorSet();
}

void Material::setOcclusionTexture(Texture* texture) {
    occlusionTexture = texture;
    properties.hasOcclusionMap = texture ? 1 : 0;
    propertiesBuffer->writeToBuffer(&properties);
    updateDescriptorSet();
}

void Material::setAlbedoColor(glm::vec4 color) {
    properties.albedoColor = color;
    propertiesBuffer->writeToBuffer(&properties);
}

void Material::setMetallic(float metallic) {
    properties.metallic = metallic;
    propertiesBuffer->writeToBuffer(&properties);
}

void Material::setSmoothness(float smoothness) {
    properties.smoothness = smoothness;
    propertiesBuffer->writeToBuffer(&properties);
}

void Material::setAO(float ao) {
    properties.ao = ao;
    propertiesBuffer->writeToBuffer(&properties);
}

void Material::setAlpha(float alpha) {
        switch (transparencyType) {
            case TransparencyType::TYPE_TRANSPARENT:
                properties.albedoColor.w = alpha;
                break;
            case TransparencyType::TYPE_MASK:
                properties.alphaCutoff = alpha;
                break;
            default:
                // Do nothing for opaque materials
                break;
        }
        
       propertiesBuffer->writeToBuffer(&properties);
}

void Material::cleanupDefaultTexture() {
    // Reset the static default texture before device destruction
    // This must be called before the Vulkan device is destroyed
    s_defaultTexture.reset();
}

} // namespace Rendering