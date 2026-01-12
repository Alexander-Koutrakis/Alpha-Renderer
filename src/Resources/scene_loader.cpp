#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4005)
#endif

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "scene_loader.hpp"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <future>
#include <fstream>
#include <sstream>
#include <ktx.h>
#include <ktxvulkan.h>

namespace Resources {

SceneLoader::SceneLoader(
    ResourceManager& resManager,
    Rendering::Device& dev)
    : resourceManager{resManager},
      device{dev},
      descriptorPool{*resManager.getPBRMaterialPool()},
      ecsManager{ECS::ECSManager::getInstance()} {}

bool SceneLoader::loadSkyboxCubemap(const std::array<std::string,6> texturesDirs) {
    try {
        // Unity skybox order: [FrontTex (Z), BackTex (-Z), RightTex (X), LeftTex (-X), UpTex (Y), DownTex (-Y)]
        // Vulkan cubemap order: [+X, -X, +Y, -Y, +Z, -Z]
        
        // Define the face order mapping from Unity to Vulkan
        const std::array<int, 6> unityToVulkanFaceMap = {
            3,  // Unity RightTex (X) -> Vulkan +X (face 0)
            2,  // Unity LeftTex (-X) -> Vulkan -X (face 1)
            4,  // Unity UpTex (Y) -> Vulkan +Y (face 2)
            5,  // Unity DownTex (-Y) -> Vulkan -Y (face 3)
            0,  // Unity FrontTex (Z) -> Vulkan +Z (face 4)
            1   // Unity BackTex (-Z) -> Vulkan -Z (face 5)
        };

        // Load the cubemap using stb_image for HDR
        std::vector<float*> faceData(6, nullptr);
        std::vector<int> widths(6, 0), heights(6, 0);
        bool allLoaded = true;

        // Load all textures in Unity order, but store them in the remapped positions
        for (int vulkanFaceIdx = 0; vulkanFaceIdx < 6; vulkanFaceIdx++) {
            // Get the Unity face index for this Vulkan face
            int unityFaceIdx = unityToVulkanFaceMap[vulkanFaceIdx];
            
            // Load the texture from the Unity-ordered path
            const auto& path = texturesDirs[unityFaceIdx];
            int width, height, channels;
            float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 4); // Force RGBA
            
            if (!data) {
                std::cout << "Failed to load face " << vulkanFaceIdx << " (Unity face " << unityFaceIdx 
                         << "): " << path << " - " << stbi_failure_reason() << std::endl;
                allLoaded = false;
                break;
            }

            // Store in the Vulkan face position
            faceData[vulkanFaceIdx] = data;
            widths[vulkanFaceIdx] = width;
            heights[vulkanFaceIdx] = height;
        }

        // Check if all faces were loaded successfully
        if (!allLoaded) {
            // Cleanup any loaded faces
            for (auto data : faceData) {
                if (data) stbi_image_free(data);
            }
            throw std::runtime_error("Failed to load all cubemap faces");
        }

        // Verify all faces have the same dimensions
        int size = widths[0];
        for (size_t i = 1; i < 6; i++) {
            if (widths[i] != size || heights[i] != size) {
                throw std::runtime_error("All cubemap faces must be square and the same size");
            }
        }

        // Create vector of face data pointers for texture creation
        std::vector<const void*> facePointers;
        for (auto data : faceData) {
            facePointers.push_back(data);
        }

        // Create the cubemap texture
        auto cubemapTexture = Rendering::Texture::createCubemap(
            device,
            size,
            VK_FORMAT_R32G32B32A32_SFLOAT, 
            facePointers
        );

        // Clean up the loaded face data
        for (auto data : faceData) {
            stbi_image_free(data);
        }

        // Add the cubemap to the resource manager
        resourceManager.addCubemap("skybox", std::move(cubemapTexture));
        
        std::cout << "Skybox loaded successfully" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Failed to load skybox cubemap: " << e.what() << std::endl;
        return false;
    }
}

void SceneLoader::createSkyboxEntity() {
    auto skyboxEntity = ecsManager.createEntity();
    ECS::SkyboxComponent skybox{skyboxEntity};
    skybox.exposure = 1.0f;
    skybox.cubemapTexture = resourceManager.getCubemap("skybox");
    ecsManager.addComponent(skyboxEntity, skybox);
    std::cout << "Skybox created" << std::endl;
}

bool SceneLoader::loadUnityScene(const std::string& jsonPath) {
    std::cout << "\n=== Starting Unity Scene Loading: " << jsonPath << " ===" << std::endl;
    
    // Read and parse JSON file
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open Unity scene file: " + jsonPath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
   
    std::cout << "Parsing scene JSON file..." << std::endl;
    DeserializedScene scene = DeserializedScene::deserialize_scene(buffer.str());
    std::cout << "Scene parsed successfully with:" << std::endl;
    std::cout << "  " << scene.meshPaths.size() << " meshes" << std::endl;
    std::cout << "  " << scene.colorTexturePaths.size() + scene.normaltexturePaths.size() << " textures" << std::endl;
    std::cout << "  " << scene.materialPaths.size() << " materials" << std::endl;
    std::cout << "  " << scene.gameObjects.size() << " game objects" << std::endl;

    //Cache all resources
    std::cout << "\nStarting resource caching..." << std::endl;
    cacheMeshes(scene.meshPaths);
    cacheCompressedTextures(scene.colorTexturePaths,KTX_TTF_BC7_RGBA, "Color textures");
    cacheCompressedTextures(scene.normaltexturePaths,KTX_TTF_BC5_RG, "Normal textures");
    cacheTextures(scene.colorTexturePaths,VK_FORMAT_R8G8B8A8_SRGB);
    cacheTextures(scene.normaltexturePaths,VK_FORMAT_R8G8B8A8_UNORM);
    loadSkyboxCubemap(scene.environmentLighting.skyboxPaths);
    cacheMaterials(scene.materialPaths); 
    std::cout << "Resource caching completed" << std::endl;

    //Create entities
    std::cout << "\nCreating entities from scene data..." << std::endl;
    size_t totalEntities = scene.gameObjects.size();
    size_t currentEntity = 0;
    for (const auto& gameObject : scene.gameObjects) {
        createEntityFromUnityData(gameObject);
        currentEntity++;
        std::cout << "\rEntities created " << currentEntity << "/" << totalEntities << std::flush;
    }
    std::cout << std::endl;
    
    std::cout << "\nSetting up scene hierarchy and lighting..." << std::endl;
    createScene(scene);
    std::cout << "Scene setup completed" << std::endl;
    
    createSkyboxEntity();
    std::cout << "\n=== Unity Scene Loading Completed Successfully ===" << std::endl;
    return true;
}

std::future<bool> SceneLoader::loadUnitySceneAsync(const std::string& jsonPath) {
    // Launch loading in background thread using std::async
    return std::async(std::launch::async, [this, jsonPath]() {
        try {
            return loadUnityScene(jsonPath);
        } catch (const std::exception& e) {
            std::cerr << "Async scene loading failed: " << e.what() << std::endl;
            return false;
        }
    });
}

void SceneLoader::cacheMeshes(const std::vector<std::string>& meshPaths) {
    size_t total = meshPaths.size();
    size_t current = 0;
    
    for (const auto& meshPath : meshPaths) {
        // Read JSON file first
        std::ifstream file(meshPath);
        if (!file.is_open()) {
            std::cerr << "\nFailed to open mesh file: " << meshPath << std::endl;
            current++;
            continue;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string jsonContent = buffer.str();
        json jsonObject = json::parse(jsonContent);
            
            // Now pass the parsed JSON object
            DeserializedMesh meshData = DeserializedMesh::from_json(jsonObject);
            std::string meshID = meshData.id;          
            // Create vertex data
            std::vector<Rendering::Mesh::Vertex> vertices;
            for (size_t i = 0; i < meshData.vertices.size(); i++) {
                Rendering::Mesh::Vertex vertex{};
                vertex.position = meshData.vertices[i];
                
                if (i < meshData.normals.size()) {
                    vertex.normal = meshData.normals[i];
                }
                
                if (i < meshData.uvs.size()) {
                    vertex.uv = meshData.uvs[i]; // Coordinates are already flipped in the mesh file uv=(uv.x,1-uv.y)
                }
                
                if (i < meshData.tangents.size()) {
                    vertex.tangent = meshData.tangents[i];
                }
                
                vertices.push_back(vertex);
            }

            std::vector<uint32_t> indices = meshData.indices;

            // Create and store mesh with debug name
            auto mesh = std::make_unique<Rendering::Mesh>(device, vertices, indices, meshID);
            for(const auto& submesh : meshData.submeshes){
                mesh->addSubmesh(submesh.indexStart, submesh.indexCount);
            }          
            
            resourceManager.addMesh(meshID, std::move(mesh));
            
            current++;
            std::cout << "\rMeshes loaded " << current << "/" << total << std::flush;       
    }
    std::cout << std::endl;
}

void SceneLoader::cacheTextures(const std::vector<std::string>& texturePaths,VkFormat format) {
    size_t total = texturePaths.size();
    size_t current = 0;
    
    for (const auto& path : texturePaths) { 
        // Check if this texture has a compressed version already loaded
        // If it exists in the map as a key, we can skip loading the regular version
        if (compressedTextureMap.find(path) != compressedTextureMap.end()) {
            current++;
            std::cout << "\rTextures cached " << current << "/" << total << std::flush;
            continue;
        }

         int width, height, channels;
            stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            
            // Extract filename for debug name
            std::filesystem::path fsPath(path);
            std::string debugName = fsPath.filename().string();
            
            auto texture = std::make_unique<Rendering::Texture>(
                device,
                width,
                height,
                format,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                pixels,
                1,  // faces
                debugName
            );
            resourceManager.addTexture(path, std::move(texture));
            current++;
            std::cout << "\rTextures cached " << current << "/" << total << std::flush;
    }
    std::cout << std::endl;
}

void SceneLoader::cacheCompressedTextures(const std::vector<std::string>& originalPaths, ktx_transcode_fmt_e targetFormat, const std::string& label) {
    std::vector<std::string> compressedPaths=getCompressedTexturePaths();
    std::vector<std::string> succesfullyLoadedCompressedTexturePaths;
    size_t total = originalPaths.size();
    size_t current = 0;
    
    // Process all KTX2 textures, only map them if they load successfully
    for (const auto& path : originalPaths) {

            std::filesystem::path fsPath(path);
            fsPath.replace_extension(".ktx2");
            std::string ktxPath = fsPath.string();

            ktxTexture2* kTexture2;
            KTX_error_code result = ktxTexture2_CreateFromNamedFile(
                ktxPath.c_str(), 
                KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, 
                &kTexture2);
                
            if (result != KTX_SUCCESS) {
                std::cerr << "\nFailed to load KTX2 texture: " << ktxPath << std::endl;
                current++;
                std::cout << "\r" << label << " cached " << current << "/" << total << std::flush;
                continue;
            }
            
            // Check if transcoding is needed and perform it
            if (ktxTexture2_NeedsTranscoding(kTexture2)) {
                result = ktxTexture2_TranscodeBasis(kTexture2, targetFormat, 0);
                if (result != KTX_SUCCESS) {
                    std::cerr << "\nFailed to transcode KTX2 texture: " << ktxPath << std::endl;
                    ktxTexture2_Destroy(kTexture2);
                    current++;
                    std::cout << "\r" << label << " cached " << current << "/" << total << std::flush;
                    continue;
                }
            }
            
            // Create Vulkan image from transcoded KTX2 texture
            ktxVulkanTexture vkTexture;
            ktxVulkanDeviceInfo vkDeviceInfo;
            ktxVulkanDeviceInfo_Construct(&vkDeviceInfo, 
                                         device.getPhysicalDevice(), 
                                         device.getDevice(), 
                                         device.getGraphicsQueue(), 
                                         device.getCommandPool(), 
                                         nullptr);
                                          
            result = ktxTexture2_VkUploadEx(kTexture2, &vkDeviceInfo, &vkTexture,
                                         VK_IMAGE_TILING_OPTIMAL,
                                         VK_IMAGE_USAGE_SAMPLED_BIT,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                         
            if (result != KTX_SUCCESS) {
                ktxTexture2_Destroy(kTexture2);
                ktxVulkanDeviceInfo_Destruct(&vkDeviceInfo);
                std::cerr << "\nFailed to upload KTX2 texture to Vulkan: " << ktxPath << std::endl;
                current++;
                std::cout << "\r" << label << " cached " << current << "/" << total << std::flush;
                continue;
            }
            
            // Create image view
            VkImageView imageView;
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = vkTexture.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = vkTexture.imageFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = vkTexture.levelCount;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = vkTexture.layerCount;
            
            if (vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
                vkTexture.vkDestroyImage(device.getDevice(), vkTexture.image, nullptr);
                vkTexture.vkFreeMemory(device.getDevice(), vkTexture.deviceMemory, nullptr);
                ktxTexture2_Destroy(kTexture2);
                ktxVulkanDeviceInfo_Destruct(&vkDeviceInfo);
                std::cerr << "\nFailed to create image view for KTX2 texture: " << ktxPath << std::endl;
                current++;
                std::cout << "\r" << label << " cached " << current << "/" << total << std::flush;
                continue;
            }
            
            // Extract filename for debug name (reuse fsPath from above)
            std::string debugName = fsPath.filename().string();
            
            // Create texture
            auto texture = std::make_unique<Rendering::Texture>(
                device,
                vkTexture.width,
                vkTexture.height,
                vkTexture.imageFormat,
                vkTexture.image,
                imageView,
                vkTexture.deviceMemory,
                vkTexture.levelCount,
                debugName
            );
            
            // Add to resource manager - Store with the KTX2 path
            resourceManager.addTexture(path, std::move(texture));         
            succesfullyLoadedCompressedTexturePaths.push_back(path);           
            // Cleanup
            ktxTexture2_Destroy(kTexture2);
            ktxVulkanDeviceInfo_Destruct(&vkDeviceInfo);
            
            current++;
            std::cout << "\r" << label << " cached " << current << "/" << total << std::flush;
    }
    std::cout << std::endl;



     for (const std::string& compressedPath : succesfullyLoadedCompressedTexturePaths) {
        std::filesystem::path compPath(compressedPath);
        std::string filename = compPath.stem().string();

        // Look for matching .png in originalPaths
        for (const std::string& origPath : originalPaths) {
            std::filesystem::path orig(origPath);
            if (orig.stem() == filename) {
                compressedTextureMap[origPath] = compressedPath;
                break; // Stop after finding the match
            }
        }
    }


}

void SceneLoader::cacheMaterials(const std::vector<std::string>& materialPaths) {
    size_t total = materialPaths.size();
    size_t current = 0;

    for (const auto& materialPath : materialPaths) {
        std::ifstream file(materialPath);
        if (!file.is_open()) {
            std::cerr << "\nFailed to open material file: " << materialPath << std::endl;
            current++;
            std::cout << "\rMaterials cached " << current << "/" << total << std::flush;
            continue;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        json matJson = json::parse(buffer.str());
        DeserializedMaterial matData = DeserializedMaterial::from_json(matJson);
        std::string materialId = matData.id;
        
        // Determine material type and pipeline
        Rendering::TransparencyType transparencyType;
        int isMasked=0;
        switch (matData.type) {
            case MaterialType::Masked:
                transparencyType = Rendering::TransparencyType::TYPE_MASK;
                isMasked=1;
                break;
            case MaterialType::Transparent:
                transparencyType = Rendering::TransparencyType::TYPE_TRANSPARENT;               
                break;
            default:
                transparencyType = Rendering::TransparencyType::TYPE_OPAQUE;
                break;
        }

        // Create material info
        Rendering::Material::MaterialInfo materialInfo{};
        materialInfo.name = materialId;
        materialInfo.transparencyType = transparencyType;
        materialInfo.enableGPUInstancing=matData.enableGPUInstancing;

        // Set and log material properties
        materialInfo.properties.albedoColor = glm::vec4(matData.albedoColor);
        materialInfo.properties.metallic = matData.metallic;
        materialInfo.properties.smoothness = matData.smoothness;
        materialInfo.properties.ao = matData.ao;
        materialInfo.properties.alphaCutoff = matData.alphaCutoff;
        materialInfo.properties.isMasked=isMasked;
        materialInfo.properties.normalStrength=matData.normalStrength;
        materialInfo.properties.hasNormalMap=matData.normalPath.empty() ? 0 : 1;
        materialInfo.properties.hasOcclusionMap=matData.occlusionPath.empty() ? 0 : 1;
        try {

            auto material = std::make_unique<Rendering::Material>(
                device,
                materialInfo,
                descriptorPool,
                resourceManager.getPBRDescriptorSetLayout()
            );


            if (!matData.albedoPath.empty()) {
                auto compressedPath=getCompressedTexturePath(matData.albedoPath);
                auto texture = resourceManager.getTexture(compressedPath);
                if(texture==nullptr){
                    cacheTextures(std::vector<std::string>{matData.albedoPath},VK_FORMAT_R8G8B8A8_SRGB);
                    texture = resourceManager.getTexture(compressedPath);
                }
                material->setAlbedoTexture(texture);
            } 

            if (!matData.normalPath.empty()) {
                auto compressedPath=getCompressedTexturePath(matData.normalPath);
                auto texture = resourceManager.getTexture(compressedPath);
                if(texture==nullptr){
                    cacheTextures(std::vector<std::string>{matData.normalPath},VK_FORMAT_R8G8B8A8_UNORM);
                    texture = resourceManager.getTexture(compressedPath);
                }
                material->setNormalTexture(texture);
            } 

            if (!matData.metallicSmoothnessPath.empty()) {
                auto compressedPath=getCompressedTexturePath(matData.metallicSmoothnessPath);
                auto texture = resourceManager.getTexture(compressedPath);
                if(texture==nullptr){
                    cacheTextures(std::vector<std::string>{matData.metallicSmoothnessPath},VK_FORMAT_R8G8B8A8_UNORM);
                    texture = resourceManager.getTexture(compressedPath);
                }
                material->setMetallicSmoothnessTexture(texture);
            }

            if (!matData.occlusionPath.empty()) {
                auto compressedPath=getCompressedTexturePath(matData.occlusionPath);
                auto texture = resourceManager.getTexture(compressedPath);
                if(texture==nullptr){
                    cacheTextures(std::vector<std::string>{matData.occlusionPath},VK_FORMAT_R8_UNORM);
                    texture = resourceManager.getTexture(compressedPath);
                }
                material->setOcclusionTexture(texture);
            }

            resourceManager.addMaterial(materialId, std::move(material));
            current++;
            std::cout << "\rMaterials cached " << current << "/" << total << std::flush;

        } catch (const std::exception& e) {
            std::cerr << "\nERROR: Failed to create material '" << materialId 
                     << "': " << e.what() << std::endl;
            current++;
            std::cout << "\rMaterials cached " << current << "/" << total << std::flush;
            throw;
        }
    }
    std::cout << std::endl;
}

void SceneLoader::createEntityFromUnityData(const DeserializedGameObject& gameObject) {
    ECS::EntityID entity = ecsManager.createEntity();
    for (auto& componentData : gameObject.components) {
        switch(componentData->componentType) {
            case ComponentType::Transform: {
                auto sTransform = std::static_pointer_cast<DeserializedTransform>(componentData);
                ECS::Transform transform{entity};
                transform.position=sTransform->position;             
                transform.rotation=sTransform->rotation;
                transform.scale = sTransform->scale;                                             
                Systems::TransformSystem::updateTransform(transform);

                ecsManager.addComponent<ECS::Transform>(entity, transform);
                break;
            }
           
            case ComponentType::Camera: {
                auto sCamera = std::static_pointer_cast<DeserializedCamera>(componentData);
                ECS::Camera camera{entity};
                camera.nearPlane = sCamera->nearPlane;
                camera.farPlane = sCamera->farPlane;
                camera.fov=glm::radians(sCamera->fieldOfView);              
                camera.aspectRatio=static_cast<float>(AlphaEngine::WIDTH) / static_cast<float>(AlphaEngine::HEIGHT);
                ecsManager.addComponent<ECS::Camera>(entity, camera);
                break;
            }
           
            case ComponentType::DirectionalLight: {
                auto sLight = std::static_pointer_cast<DeserializedDirectionalLight>(componentData);
                
                ECS::DirectionalLight directionalLight(
                    entity,
                    sLight->intensity,
                    sLight->color,
                    sLight->direction,
                    sLight->isCastingShadows,
                    sLight->shadowStrength
                );
                ecsManager.addComponent<ECS::DirectionalLight>(entity, directionalLight);
                break;
            }

            case ComponentType::SpotLight: {
                auto sLight = std::static_pointer_cast<DeserializedSpotLight>(componentData);
                auto transform=ecsManager.getComponent<ECS::Transform>(entity);
                ECS::SpotLight spotLight{
                    entity,
                    sLight->intensity,
                    sLight->range,
                    sLight->innerCutoff,
                    sLight->outerCutoff,
                    sLight->color,
                    sLight->isCastingShadows,
                    sLight->shadowStrength
                };  
                spotLight.transform=*transform;

                ecsManager.addComponent<ECS::SpotLight>(entity, spotLight);
                ecsManager.removeComponent<ECS::Transform>(entity);
                break;
            }

            case ComponentType::PointLight: {
                auto sLight = std::static_pointer_cast<DeserializedPointLight>(componentData);
                auto transform=ecsManager.getComponent<ECS::Transform>(entity);
                ECS::PointLight pointLight{
                    entity,
                    sLight->intensity,
                    sLight->range,
                    sLight->color,
                    sLight->isCastingShadows,
                    sLight->shadowStrength
                };  
                pointLight.transform=*transform;
                ecsManager.addComponent<ECS::PointLight>(entity, pointLight);
                ecsManager.removeComponent<ECS::Transform>(entity);

                Systems::TransformSystem::updateTransform(pointLight.transform);

                break;
            }
           
            case ComponentType::MeshRenderer: {
                auto sMeshRenderer = std::static_pointer_cast<DeserializedMeshRenderer>(componentData);
                ECS::Transform* transform = ecsManager.getComponent<ECS::Transform>(entity);               
  
                auto mesh = resourceManager.getMesh(sMeshRenderer->meshId);
                uint32_t materialCount=sMeshRenderer->materialIds.size();
                std::vector<Rendering::Material*> materials;
                for(uint32_t i=0;i<materialCount;i++){
                    auto materialId=sMeshRenderer->materialIds[i];
                    auto material=resourceManager.getMaterial(materialId);
                    materials.push_back(material);
                }

                ECS::MeshRenderer meshRenderer(entity,mesh,materials,sMeshRenderer->castingShadows);
                ECS::Renderable renderable{entity};
                renderable.transform=*transform;
                renderable.meshRenderer=meshRenderer;
                Systems::TransformSystem::updateTransform(renderable.transform);
                
                ecsManager.addComponent<ECS::Renderable>(entity, renderable);   
                ecsManager.removeComponent<ECS::Transform>(entity);               
                break;             
            }
        }
    } 
}

void SceneLoader::createScene(const Resources::DeserializedScene& deserializedScene){
    auto& scene=Scene::Scene::getInstance();
    auto spotLights=ecsManager.getAllComponents<ECS::SpotLight>();
    auto pointLights=ecsManager.getAllComponents<ECS::PointLight>();
    auto renderers=ecsManager.getAllComponents<ECS::Renderable>();
   
    for(uint32_t i=0;i<spotLights.size();i++){
         scene.addLight(static_cast<ECS::Light&>(*spotLights[i]));
    }

    for(uint32_t i=0;i<pointLights.size();i++){
        scene.addLight(static_cast<ECS::Light&>(*pointLights[i]));
    }

    for(uint32_t i=0;i<renderers.size();i++){
        scene.addRenderer(static_cast<Renderable&>(*renderers[i]));
    }


    Scene::EnvironmentLighting envLighting{
        deserializedScene.environmentLighting.ambientColor,
        deserializedScene.environmentLighting.ambientIntensity,
        resourceManager.getCubemap("skybox"),
        deserializedScene.environmentLighting.reflectionIntensity
    };

    scene.setEnvironmentLighting(&envLighting);
}

std::string SceneLoader::getCompressedTexturePath(const std::string& texturePath){
    auto it=compressedTextureMap.find(texturePath);
    if(it!=compressedTextureMap.end()){
        return it->second;
    }
    return texturePath;
}

std::vector<std::string> SceneLoader::getCompressedTexturePaths() {
    const std::string directory = "Assets/Scene/textures";
    std::vector<std::string>compressedTexturePaths{};
     if (!std::filesystem::exists(directory)) {
            std::cerr << "Directory does not exist: " << directory << std::endl;
            return compressedTexturePaths;
        }
        
        // Iterate through directory
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                // Check if file has .ktx2 extension
                if (path.find(".ktx2") != std::string::npos) {
                    compressedTexturePaths.push_back(path);
                }
            }
        }              
        return compressedTexturePaths;
   
}
} // namespace Resources


