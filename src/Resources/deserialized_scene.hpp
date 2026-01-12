#pragma once
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include "external/libraries/json.hpp"
#include "core.hpp"
using json = nlohmann::json;



namespace Resources {
enum class MaterialType {
    Opaque = 0,
    Masked = 1,
    Transparent = 2
};

enum class ComponentType{
    Transform=0,
    Camera=1,
    DirectionalLight=2,
    SpotLight=3,
    PointLight=4,
    MeshRenderer=5
};

struct DeserializedMaterial {
    std::string id;
    MaterialType type;
    std::string albedoPath;
    std::string normalPath;
    std::string metallicSmoothnessPath;
    std::string heightPath;
    std::string occlusionPath;
    std::string detailMaskPath;
    glm::vec4 albedoColor;
    float metallic;
    float smoothness;
    float ao;
    float alpha;
    float alphaCutoff;
    float normalStrength;
    bool enableGPUInstancing;
    static DeserializedMaterial from_json(const json& j);
};

struct DeserializedMesh {
    std::string id;
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> tangents;
    std::vector<uint32_t> indices;
    struct Submesh{
        uint32_t indexStart;
        uint32_t indexCount;
    };
    std::vector<Submesh> submeshes;
    static DeserializedMesh from_json(const json& j);
};

struct Component {
    virtual ~Component() = default;
    ComponentType componentType{ComponentType::Transform};
};

struct DeserializedTransform : Component {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
    
    static std::shared_ptr<DeserializedTransform> from_json(const json& j);
};

struct DeserializedCamera : Component {
    float fieldOfView;
    float nearPlane;
    float farPlane;

    static std::shared_ptr<DeserializedCamera> from_json(const json& j);
};

struct DeserializedMeshRenderer : Component {
    std::string meshId;
    std::vector<std::string> materialIds;
    bool castingShadows;
    
    static std::shared_ptr<DeserializedMeshRenderer> from_json(const json& j);
};

struct DeserializedDirectionalLight : Component {
    glm::vec4 direction;
    glm::vec3 color;
    float intensity;
    bool isCastingShadows;
    float shadowStrength;
    static std::shared_ptr<DeserializedDirectionalLight> from_json(const json& j);
};

struct DeserializedSpotLight : Component {
        float intensity{1.0f};     // Light intensity
        float range{10.0f};        // How far the light reaches
        float innerCutoff{12.5f};  // Inner cone angle in degrees (where light is at full intensity)
        float outerCutoff{17.5f};  // Outer cone angle in degrees (where light begins to fade out)
        glm::vec3 color{1.0f};     // Light color (RGB)
        bool isCastingShadows;
        float shadowStrength;
    static std::shared_ptr<DeserializedSpotLight> from_json(const json& j);
};

struct DeserializedPointLight : Component {
        float intensity{1.0f};
        float range{1.0f};
        glm::vec3 color{1.0f};
        bool isCastingShadows;
        float shadowStrength;
    static std::shared_ptr<DeserializedPointLight> from_json(const json& j);
};

struct DeserializedGameObject {
        int entityId;
        std::vector<std::shared_ptr<Component>> components;
        
        static DeserializedGameObject from_json(const json& j) {
            DeserializedGameObject go;
            go.entityId = j["EntityID"].get<int>();
            
            const auto& components_json = j["components"];
            for (const auto& comp_json : components_json) {
                std::string type = comp_json["$type"].get<std::string>();
                
                if (type.find("sTransform") != std::string::npos) {               
                    go.components.push_back(DeserializedTransform::from_json(comp_json));
                }else if (type.find("sCamera") != std::string::npos) {
                    go.components.push_back(DeserializedCamera::from_json(comp_json));
                }else if (type.find("sMeshRenderer") != std::string::npos) {
                    go.components.push_back(DeserializedMeshRenderer::from_json(comp_json));
                }else if (type.find("sDirectionalLight") != std::string::npos) {
                    go.components.push_back(DeserializedDirectionalLight::from_json(comp_json));
                }else if(type.find("sSpotLight") != std::string::npos) {
                    go.components.push_back(DeserializedSpotLight::from_json(comp_json));
                }else if(type.find("sPointLight") != std::string::npos) {
                    go.components.push_back(DeserializedPointLight::from_json(comp_json));
                }             
            }
            return go;
        }
    };

struct EnvironmentLighting {
    glm::vec3 ambientColor;
    float ambientIntensity;
    std::array<std::string,6>skyboxPaths;  
    float reflectionIntensity;
    static EnvironmentLighting from_json(const json& j);
};

struct DeserializedScene {
            std::vector<std::string>colorTexturePaths;
            std::vector<std::string>normaltexturePaths; 
            std::vector<std::string> meshPaths;         
            std::vector<std::string> materialPaths;            
            std::vector<DeserializedGameObject> gameObjects;
            EnvironmentLighting environmentLighting;

            static DeserializedScene deserialize_scene(const std::string& jsonStr);
        };


}
