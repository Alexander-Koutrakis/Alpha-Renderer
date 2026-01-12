#pragma once
#include "Rendering/Resources/mesh.hpp"
#include "Rendering/Resources/material.hpp"
#include "Rendering/Resources/texture.hpp"
#include "core.hpp"
#include <array>
#include "ecs_types.hpp"
using namespace Rendering;
using namespace Systems;

namespace ECS{
    // Move INVALID_ENTITY_ID inside the ECS namespace where EntityID is defined
    static constexpr EntityID INVALID_ENTITY_ID = std::numeric_limits<EntityID>::max();

    enum LightType {
        DIRECTIONAL_LIGHT = 0,
        SPOT_LIGHT = 1,
        POINT_LIGHT = 2
    };

    struct Component {
        EntityID owner;
        Component(EntityID owner=INVALID_ENTITY_ID):owner(owner){}
        virtual ~Component() = default;
    };

    struct Transform : public Component {
        glm::vec3 position{0.0f};           
        glm::quat rotation{1.0f, 0, 0, 0};  
        glm::vec3 scale{1.0f};             
        glm::mat4 modelMatrix{1.0f};
        glm::mat4 normalMatrix{1.0f};
        Transform(EntityID owner):Component(owner){}
        Transform() : Component(INVALID_ENTITY_ID) {}
    };

    // Rest of your components with default constructors...
    struct MeshRenderer:public Component{
        Mesh* mesh;
        std::vector<Material*> materials{nullptr};
        bool castingShadows{false}; 
        MeshRenderer(EntityID owner,Mesh* mesh, std::vector<Material*> materials, bool castingShadows = false):
        Component(owner),
        mesh(mesh),
        materials(materials),
        castingShadows(castingShadows){};
        
        // Default constructor for array allocation
        MeshRenderer(EntityID owner) : Component(owner), mesh(nullptr), materials(), castingShadows(false) {}
        MeshRenderer() : Component(INVALID_ENTITY_ID), mesh(nullptr), materials(), castingShadows(false) {}
    };

    struct Light : public Component {   
        float intensity{1.0f};
        glm::vec3 color{1.0f};
        bool isCastingShadows{false};
        float shadowStrength{1.0f};
        LightType type;
               
        Light(EntityID owner,LightType lightType, float lightIntensity = 1.0f, glm::vec3 lightColor = glm::vec3(1.0f), bool castShadows = false, float shadowStrength = 1.0f)
            : Component(owner), intensity(lightIntensity), color(lightColor), isCastingShadows(castShadows), shadowStrength(shadowStrength), type(lightType) {}
        
        // Add default constructor for Light
        Light() : Component(INVALID_ENTITY_ID), intensity(1.0f), color(1.0f), isCastingShadows(false), shadowStrength(1.0f), type(DIRECTIONAL_LIGHT) {}
    };

    struct DirectionalLight : public Light {
        glm::vec4 direction{1.0f, 0, 0, 0};
        std::array<glm::mat4,MAX_SHADOW_CASCADE_COUNT> viewProjectionMatrix;
        std::array<float,MAX_SHADOW_CASCADE_COUNT> cascadeSplits;

        DirectionalLight(EntityID owner,float lightIntensity = 1.0f, glm::vec3 lightColor = glm::vec3(1.0f), glm::vec4 lightDirection = glm::vec4(1.0f, 0, 0, 0), bool castShadows = false, float shadowStrength = 1.0f)
            : Light(owner,LightType::DIRECTIONAL_LIGHT, lightIntensity, lightColor, castShadows, shadowStrength), direction(lightDirection) {};            
    
        DirectionalLight() : Light(INVALID_ENTITY_ID, LightType::DIRECTIONAL_LIGHT, 1.0f, glm::vec3(1.0f), false), direction(glm::vec4(1.0f, 0, 0, 0)) {}
    };

    struct SpotLight : public Light {
        float range{10.0f};
        float innerCutoff{12.5f};
        float outerCutoff{17.5f};
        glm::mat4 viewProjectionMatrix{1.0f};
        Transform transform;
        SpotLight(EntityID owner,float lightIntensity = 1.0f, float lightRange = 10.0f, 
                  float lightInnerCutoff = 12.5f, float lightOuterCutoff = 17.5f,
                  glm::vec3 lightColor = glm::vec3(1.0f), bool castShadows = false, float shadowStrength = 1.0f)
            : Light(owner,LightType::SPOT_LIGHT, lightIntensity, lightColor, castShadows, shadowStrength),
              range(lightRange), innerCutoff(lightInnerCutoff), outerCutoff(lightOuterCutoff), transform(owner) {}
        SpotLight() : Light(INVALID_ENTITY_ID, LightType::SPOT_LIGHT, 1.0f, glm::vec3(1.0f), false, 1.0f), range(10.0f), innerCutoff(12.5f), outerCutoff(17.5f), transform(INVALID_ENTITY_ID) {}
    };
    
    struct PointLight : public Light {       
        float range{1.0f};
        std::array<glm::mat4,6> viewProjectionMatrix;
        Transform transform;
        PointLight(EntityID owner,float lightIntensity = 1.0f, float lightRange = 1.0f,
                   glm::vec3 lightColor = glm::vec3(1.0f), bool castShadows = false, float shadowStrength = 1.0f)
            : Light(owner,LightType::POINT_LIGHT, lightIntensity, lightColor, castShadows, shadowStrength),
              range(lightRange), transform(owner) {}
        PointLight() : Light(INVALID_ENTITY_ID, LightType::POINT_LIGHT, 1.0f, glm::vec3(1.0f), false, 1.0f), range(1.0f), transform(INVALID_ENTITY_ID) {}
    };

    struct Camera : public Component {
        glm::mat4 projectionMatrix{1.0f};  
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 viewProjectionMatrix{1.0f};
        float fov{glm::radians(45.0f)};
        float aspectRatio{1.3333f};
        float nearPlane{0.1f};
        float farPlane{1000.0f};
        Camera(EntityID owner):Component(owner){}
        Camera() : Component(INVALID_ENTITY_ID) {}
    };

    struct SkyboxComponent : public Component {
        Texture* cubemapTexture{nullptr};
        float exposure{1.0f};
        SkyboxComponent(EntityID owner):Component(owner){}
        SkyboxComponent() : Component(INVALID_ENTITY_ID) {}
    };

    struct Renderable:public Component{
        Transform transform;
        MeshRenderer meshRenderer;       
        Renderable(EntityID owner):Component(owner),transform(owner),meshRenderer(owner,nullptr,std::vector<Material*>(),false){}
        Renderable() : Component(INVALID_ENTITY_ID), transform(INVALID_ENTITY_ID), meshRenderer(INVALID_ENTITY_ID, nullptr, std::vector<Material*>(), false) {}
    };

}