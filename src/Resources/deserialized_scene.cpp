#include "deserialized_scene.hpp"
namespace glm {
    static vec2 from_json_vec2(const json& j) {
        return vec2{
            j["x"].get<float>(),
            j["y"].get<float>()
        };
    }

    static vec3 from_json_vec3(const json& j) {
        return vec3{
            j["x"].get<float>(),
            j["y"].get<float>(),
            j["z"].get<float>()
        };
    }

    static vec4 from_json_vec4(const json& j){
        return vec4{
            j["x"].get<float>(),
            j["y"].get<float>(),
            j["z"].get<float>(),
            j["w"].get<float>()
        };
    }

    static quat from_json_quat(const json& j) {
        return quat{
        j["w"].get<float>(),  // w component (scalar) comes first in GLM
        j["x"].get<float>(),  // x component
        j["y"].get<float>(),  // y component
        j["z"].get<float>()   // z component
    };
}
}
namespace Resources {
    DeserializedScene DeserializedScene::deserialize_scene(const std::string& jsonStr) {
        DeserializedScene scene;
        json j = json::parse(jsonStr);
        
        const auto& colorTextures_json = j["ColorTexturePaths"];
        scene.colorTexturePaths.reserve(colorTextures_json.size());
        for (const auto& colorTexturePath_json : colorTextures_json) {
            scene.colorTexturePaths.push_back(colorTexturePath_json);
        }

        const auto& normalTextures_json = j["NormalTexturePaths"];
        scene.normaltexturePaths.reserve(normalTextures_json.size());
        for (const auto& normaltexturePaths : normalTextures_json) {
            scene.normaltexturePaths.push_back(normaltexturePaths);
        }

        const auto& meshPaths_json = j["MeshPaths"];
        scene.meshPaths.reserve(meshPaths_json.size());
        for (const auto& meshPath_json : meshPaths_json) {
            scene.meshPaths.push_back(meshPath_json);
        }

        if (j.contains("MaterialPaths")) {
            const auto& materials_json = j["MaterialPaths"];
            scene.materialPaths.reserve(materials_json.size());
            for (const auto& materialPath : materials_json) {
                scene.materialPaths.push_back(materialPath.get<std::string>());
            }
        }
        
        const auto& gameObjects_json = j["Gameobjects"];
        scene.gameObjects.reserve(gameObjects_json.size());
        for (const auto& go_json : gameObjects_json) {
            scene.gameObjects.push_back(DeserializedGameObject::from_json(go_json));
        }

        const auto& environmentLighting_json = j["EnviromentLighting"];
        scene.environmentLighting = EnvironmentLighting::from_json(environmentLighting_json);
       
        return scene;
    }

    DeserializedMaterial DeserializedMaterial::from_json(const json& j) {
        DeserializedMaterial m;
        
        // Required fields with default values
        m.id = j.contains("ID") ? j["ID"].get<std::string>() : "unknown";
        m.type = j.contains("Type") ? static_cast<MaterialType>(j["Type"].get<int>()) : MaterialType::Opaque;
        
        // Optional texture paths with empty string defaults
        m.albedoPath = j.contains("AlbedoPath") && !j["AlbedoPath"].is_null() ? 
            j["AlbedoPath"].get<std::string>() : "";
        m.normalPath = j.contains("NormalPath") && !j["NormalPath"].is_null() ? 
            j["NormalPath"].get<std::string>() : "";
        m.metallicSmoothnessPath = j.contains("MetallicSmoothnessPath") && !j["MetallicSmoothnessPath"].is_null() ? 
            j["MetallicSmoothnessPath"].get<std::string>() : "";
        m.heightPath= j.contains("HeightPath") && !j["HeightPath"].is_null() ? 
            j["HeightPath"].get<std::string>() : "";
        m.occlusionPath=j.contains("OcclusionPath") && !j["OcclusionPath"].is_null() ? 
            j["OcclusionPath"].get<std::string>() : "";
        m.detailMaskPath=j.contains("DetailMaskPath") && !j["DetailMaskPath"].is_null() ? 
            j["DetailMaskPath"].get<std::string>() : "";

        // Material properties with default values
        m.albedoColor = j.contains("AlbedoColor") ? glm::from_json_vec4(j["AlbedoColor"]) : 
            glm::vec4(1.0f, 1.0f, 1.0f,1.0f);
        m.metallic = j.contains("Mettalic") ? j["Mettalic"].get<float>() : 0.0f;
        m.smoothness = j.contains("Smoothness") ? j["Smoothness"].get<float>() : 0.5f;
        m.ao = j.contains("AO") ? j["AO"].get<float>() : 1.0f;
        m.alpha = j.contains("Alpha") ? j["Alpha"].get<float>() : 1.0f;
        m.alphaCutoff=j.contains("AlphaCutoff")?j["AlphaCutoff"].get<float>():1.0f;
        m.normalStrength=j.contains("NormalStrength")?j["NormalStrength"].get<float>():1.0f;
        m.enableGPUInstancing=j.contains("EnableGPUInstancing")?j["EnableGPUInstancing"].get<bool>():false;
        return m;
    }
   
   DeserializedMesh DeserializedMesh::from_json(const json& j) {
        DeserializedMesh m;
        m.id = j.contains("ID") ? j["ID"].get<std::string>() : "unknown";
        
        auto vertices_json = j["Vertices"];
        m.vertices.reserve(vertices_json.size());
        for (const auto& v : vertices_json) {
            m.vertices.push_back(glm::from_json_vec3(v));
        }
        
        auto normals_json = j["Normals"];
        m.normals.reserve(normals_json.size());
        for (const auto& n : normals_json) {
            m.normals.push_back(glm::from_json_vec3(n));
        }
        
        auto uvs_json = j["UVs"];
        m.uvs.reserve(uvs_json.size());
        for (const auto& uv : uvs_json) {
            m.uvs.push_back(glm::from_json_vec2(uv));
        }

        auto tangents_json=j["Tangents"];
        m.tangents.reserve(tangents_json.size());
        for(const auto& tangent : tangents_json){
            m.tangents.push_back(glm::from_json_vec4(tangent));
        }
        
        m.indices = j["Indices"].get<std::vector<uint32_t>>();
        auto submeshes_json=j["SubMeshes"];
        m.submeshes.reserve(submeshes_json.size());
        for(const auto& submesh : submeshes_json){
            m.submeshes.push_back(Submesh{submesh["IndexStart"].get<uint32_t>(),submesh["IndexCount"].get<uint32_t>()});
        }
        return m;
    }

   std::shared_ptr<DeserializedTransform> DeserializedTransform::from_json(const json& j) {
        auto transform = std::make_shared<DeserializedTransform>();
        transform->position = glm::from_json_vec3(j["Position"]);
        transform->rotation = glm::from_json_quat(j["Rotation"]);
        transform->scale = glm::from_json_vec3(j["Scale"]);
        transform->componentType=ComponentType::Transform;
        return transform;
    }
  
EnvironmentLighting EnvironmentLighting::from_json(const json& j) {
    EnvironmentLighting env;
    
    env.ambientColor = glm::from_json_vec3(j["Color"]);
    env.ambientIntensity = j["AmbientIntensity"].get<float>();
    env.skyboxPaths = j["SkyboxPaths"].get<std::array<std::string,6>>();
    env.reflectionIntensity = j["ReflectionIntensity"].get<float>();
    return env;
}

  std::shared_ptr<DeserializedCamera>DeserializedCamera::from_json(const json& j) {
        auto camera = std::make_shared<DeserializedCamera>();
        camera->fieldOfView = j["FieldOfView"].get<float>();
        camera->nearPlane = j["NearPlane"].get<float>();
        camera->farPlane = j["FarPlane"].get<float>();
        camera->componentType=ComponentType::Camera;
        return camera;

    }

 std::shared_ptr<DeserializedMeshRenderer> DeserializedMeshRenderer::from_json(const json& j) {
    auto meshRenderer = std::make_shared<DeserializedMeshRenderer>();
    meshRenderer->meshId = j["MeshID"].get<std::string>();
    meshRenderer->componentType = ComponentType::MeshRenderer;
    meshRenderer->castingShadows = j["CastingShadows"];
    for (const auto& matId : j["MaterialIDs"]) {
        meshRenderer->materialIds.push_back(matId.get<std::string>());
    }
   
    
    return meshRenderer;
}

  std::shared_ptr<DeserializedDirectionalLight> DeserializedDirectionalLight::from_json(const json& j) {
        auto light = std::make_shared<DeserializedDirectionalLight>();

        glm::quat rotation = glm::from_json_quat(j["Direction"]);
        glm::vec3 direction = glm::rotate(rotation, glm::vec3(0.0f, 0.0f, 1.0f));
        light->direction = glm::vec4(direction, 0.0f);
        light->color = glm::from_json_vec3(j["Color"]);
        light->intensity = j["Intensity"].get<float>();
        light->componentType=ComponentType::DirectionalLight;
        light->isCastingShadows=j["IsCastingShadows"];
        light->shadowStrength=j["ShadowStrength"].get<float>();
        return light;
    }

   std::shared_ptr<DeserializedSpotLight> DeserializedSpotLight::from_json(const json& j) {
        auto light = std::make_shared<DeserializedSpotLight>();
        light->intensity = j["Intensity"].get<float>();
        light->range = j["Range"].get<float>();
        light->innerCutoff=j["InnerCutoff"].get<float>();
        light->outerCutoff=j["OuterCutoff"].get<float>();
        light->color = glm::from_json_vec3(j["Color"]);    
        light->componentType=ComponentType::SpotLight;
        light->isCastingShadows=j["IsCastingShadows"];
        light->shadowStrength=j["ShadowStrength"].get<float>();
        return light;
    }

    std::shared_ptr<DeserializedPointLight> DeserializedPointLight::from_json(const json& j) {
        auto light = std::make_shared<DeserializedPointLight>();
        light->intensity = j["Intensity"].get<float>();
        light->range = j["Range"].get<float>();
        light->color = glm::from_json_vec3(j["Color"]);
        light->isCastingShadows=j["IsCastingShadows"];
        light->shadowStrength=j["ShadowStrength"].get<float>();
        light->componentType=ComponentType::PointLight;
        return light;
    }
}