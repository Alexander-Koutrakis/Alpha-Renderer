#pragma once

#include "core.hpp"

#include "Rendering/Resources/mesh.hpp"
#include "Rendering/Resources/material.hpp"
#include "Rendering/Resources/texture.hpp"
#include "resource_manager.hpp"
#include "Rendering/Core/device.hpp"
#include "Rendering/Core/descriptors.hpp"
#include "Scene/scene.hpp"
#include "deserialized_scene.hpp"
#include "ECS/ecs.hpp"
#include "Systems/transform_system.hpp"
#include "external/libraries/tiny_gltf.h"
#include "Engine/alpha_engine.hpp"
#include "Systems/light_system.hpp"//initialize spot and point light matrices

#include <fstream>
#include "external/libraries/json.hpp"
#include <string>
#include <memory>
#include <vector>
#include <future>
#include <unordered_map>

namespace Resources {
    class SceneLoader {
    public:
        SceneLoader(
            ResourceManager& resourceManager, 
            Rendering::Device& device
        );
        ~SceneLoader() = default;

        SceneLoader(const SceneLoader&) = delete;
        SceneLoader& operator=(const SceneLoader&) = delete;

        bool loadUnityScene(const std::string& jsonPath);
        
        // Async version of the scene loader
        std::future<bool> loadUnitySceneAsync(const std::string& jsonPath);

        

    private:
        void cacheMeshes(const std::vector<std::string>& meshPaths);
        void cacheTextures(const std::vector<std::string>& colorTexturePaths,VkFormat format);
        void cacheCompressedTextures(const std::vector<std::string>& pngPaths,ktx_transcode_fmt_e targetFormat=KTX_TTF_BC7_RGBA, const std::string& label="Compressed textures");
        void cacheMaterials(const std::vector<std::string>& materialPaths);
        void createEntityFromUnityData(const Resources::DeserializedGameObject& gameObject);
        void createScene(const Resources::DeserializedScene& deserializedScene);
        bool loadSkyboxCubemap(const std::array<std::string,6> texturesDirs);
        std::vector<std::string> getCompressedTexturePaths();
        void createSkyboxEntity();
        
        std::string getCompressedTexturePath(const std::string& texturePath);
        ResourceManager& resourceManager;
        Rendering::Device& device;
        Rendering::DescriptorPool& descriptorPool;
        ECS::ECSManager& ecsManager;
        std::string basePath;
        std::unordered_map<std::string, std::string> compressedTextureMap;
    };
} // namespace Resources


