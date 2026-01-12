#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include "Rendering/Core/device.hpp"
#include "Rendering/Resources/mesh.hpp"
#include "Rendering/Resources/texture.hpp"
#include "Rendering/Resources/material.hpp"
#include "Rendering/Core/swapchain.hpp"
#include <array>
using namespace Rendering;
namespace Resources {
class ResourceManager {
public:
    ResourceManager(Device& device);
    ~ResourceManager() = default;

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) = delete;
    ResourceManager& operator=(ResourceManager&&) = delete;

    // Resource management
    void addMesh(const std::string& name, std::unique_ptr<Rendering::Mesh> mesh);
    void addTexture(const std::string& name, std::unique_ptr<Rendering::Texture> texture);
    void addMaterial(const std::string& name, std::unique_ptr<Rendering::Material> material);
    void addCubemap(const std::string& name, std::unique_ptr<Rendering::Texture> cubemap);
    
    // Resource retrieval
    Rendering::Mesh* getMesh(const std::string& name);
    Rendering::Texture* getTexture(const std::string& name);
    Rendering::Material* getMaterial(const std::string& name);
    Rendering::Texture* getCubemap(const std::string& name);
    
    // Resource unloading
    void unloadMesh(const std::string& name);
    void unloadTexture(const std::string& name);
    void unloadMaterial(const std::string& name);
    void unloadCubemap(const std::string& name);

    void unloadAllMeshes();
    void unloadAllTextures();
    void unloadAllMaterials();
    void unloadAllCubemaps();
    void cleanup();

    DescriptorPool* getPBRMaterialPool() { return pbrMaterialDescriptorPool.get();}
    VkDescriptorSetLayout getPBRDescriptorSetLayout()const{ return pbrDescriptorSetLayout;}
private:
    void createMaterialDescriptorPool();
    void createPBRDescriptorSetLayout();

    Device& device;

    std::unordered_map<std::string, std::unique_ptr<Rendering::Mesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<Rendering::Texture>> textures;
    std::unordered_map<std::string, std::unique_ptr<Rendering::Material>> materials;
    std::unordered_map<std::string, std::unique_ptr<Rendering::Texture>> cubemaps;

    std::unique_ptr<DescriptorPool> pbrMaterialDescriptorPool{nullptr};
    VkDescriptorSetLayout pbrDescriptorSetLayout{VK_NULL_HANDLE};
};
} // namespace Resources