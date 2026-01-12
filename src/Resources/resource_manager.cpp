#include "resource_manager.hpp"
#include <stdexcept>
#include <iostream>
namespace Resources {

    ResourceManager::ResourceManager(Device& device):device(device){
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
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxMaterials},           // Material UBOs
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxTextures}     // Textures
        };

        pbrMaterialDescriptorPool= DescriptorPool::Builder(device)
            .setMaxSets(maxMaterials)
            .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxMaterials)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxTextures)
            .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
            .build();
    }

    void ResourceManager::createPBRDescriptorSetLayout() {
        // Material UBO binding
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Texture bindings
        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
        bindings[0] = uboBinding;

        // Albedo texture
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Normal map
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Metallic-roughness map
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Occlusion map
        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device.getDevice(), &layoutInfo, nullptr, &pbrDescriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create PBR descriptor set layout!");
        }
    }


} 

