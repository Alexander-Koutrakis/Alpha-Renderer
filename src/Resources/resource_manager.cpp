#include "resource_manager.hpp"
#include <stdexcept>
namespace Resources {

ResourceManager::ResourceManager(Device& device) : device(device) {
    createMaterialDescriptorPool();
    createPBRDescriptorSetLayout();
}

void ResourceManager::addMesh(const std::string& name, std::unique_ptr<Rendering::Mesh> mesh) {
    if (meshes.find(name) != meshes.end()) {
        throw std::runtime_error("Mesh '" + name + "' already exists!");
    }
    meshes[name] = std::move(mesh);
}

void ResourceManager::addTexture(const std::string& name, std::unique_ptr<Rendering::Texture> texture) {
    if (textures.find(name) != textures.end()) {
        throw std::runtime_error("Texture '" + name + "' already exists!");
    }
    textures[name] = std::move(texture);
}

void ResourceManager::addMaterial(const std::string& name, std::unique_ptr<Rendering::Material> material) {
    if (materials.find(name) != materials.end()) {
        throw std::runtime_error("Material '" + name + "' already exists!");
    }
    materials[name] = std::move(material);
}

Rendering::Mesh* ResourceManager::getMesh(const std::string& name) {
    auto it = meshes.find(name);
    if (it == meshes.end()) {
        return nullptr;
    }
    return it->second.get();
}

Rendering::Texture* ResourceManager::getTexture(const std::string& name) {
    auto it = textures.find(name);
    if (it == textures.end()) {
        return nullptr;
    }
    return it->second.get();
}

Rendering::Material* ResourceManager::getMaterial(const std::string& name) {
    auto it = materials.find(name);
    if (it == materials.end()) {
        return nullptr;
    }
    return it->second.get();
}

void ResourceManager::unloadMesh(const std::string& name) {
    meshes.erase(name);
}

void ResourceManager::unloadTexture(const std::string& name) {
    textures.erase(name);
}

void ResourceManager::unloadMaterial(const std::string& name) {
    materials.erase(name);
}

void ResourceManager::unloadAllMeshes() {
    meshes.clear();
}

void ResourceManager::unloadAllTextures() {
    textures.clear();
}

void ResourceManager::unloadAllMaterials() {
    materials.clear();
}

void ResourceManager::unloadCubemap(const std::string& name) {
    cubemaps.erase(name);
}

void ResourceManager::unloadAllCubemaps() {
    cubemaps.clear();
}

void ResourceManager::cleanup() {
    unloadAllMeshes();
    unloadAllTextures();
    unloadAllMaterials();
    unloadAllCubemaps();

    // Clean up the static default texture used by all materials
    // This must be done before the device is destroyed
    Material::cleanupDefaultTexture();

    // Clean up descriptor set layout
    if (pbrDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.getDevice(), pbrDescriptorSetLayout, nullptr);
        pbrDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void ResourceManager::addCubemap(const std::string& name, std::unique_ptr<Rendering::Texture> cubemap) {
    if (cubemaps.find(name) != cubemaps.end()) {
        throw std::runtime_error("Cubemap '" + name + "' already exists!");
    }
    cubemaps[name] = std::move(cubemap);
}

Rendering::Texture* ResourceManager::getCubemap(const std::string& name) {
    auto it = cubemaps.find(name);
    if (it == cubemaps.end()) {
        return nullptr;
    }
    return it->second.get();
}

void ResourceManager::createMaterialDescriptorPool() {
    // Calculate descriptor counts - increased to handle larger scenes
    const uint32_t maxMaterials = 500;
    const uint32_t maxTextures = maxMaterials * 4; // 4 textures per material

    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxMaterials},       // Material UBOs
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxTextures} // Textures
    };

    pbrMaterialDescriptorPool = DescriptorPool::Builder(device)
                                    .setMaxSets(maxMaterials)
                                    .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxMaterials)
                                    .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxTextures)
                                    .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
                                    .build();
}

void ResourceManager::createPBRDescriptorSetLayout() {
    // Material UBO, then albedo, normal, metallic-smoothness and occlusion textures.
    pbrDescriptorSetLayout = Rendering::createDescriptorSetLayout(
        device,
        {
            Rendering::layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
            Rendering::layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            Rendering::layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            Rendering::layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            Rendering::layoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        });
}

} // namespace Resources
