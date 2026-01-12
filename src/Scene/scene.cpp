#include "Scene/scene.hpp"
#include <iostream>

using namespace ECS;
using namespace Systems;
namespace Scene{

    void Scene::addRenderer(Renderable& renderable){
        auto meshRenderer=renderable.meshRenderer;
        auto transform=renderable.transform;

        AABB worldAABB{};
        auto localBounds=meshRenderer.mesh->getLocalBounds();
        BoundingBoxSystem::getWorldBounds(worldAABB,localBounds,transform.modelMatrix);          

        // Create object in the octree and store a reference to it
        auto* octreeObject = rendererTree.createObject(&renderable, worldAABB);
        rendererMap[&renderable] = octreeObject;
    }

    void Scene::createPointLightAABB(PointLight& light, AABB& worldAABB){
        auto transform=light.transform;
        worldAABB.center = transform.position;
        worldAABB.extents = glm::vec3(light.range);
    }

    void Scene::createSpotLightAABB(SpotLight& light, AABB& worldAABB) {
        // Spotlight's position
        auto transform=light.transform;
        glm::vec3 position = transform.position;
        // Spotlight's direction
        glm::vec3 direction = TransformSystem::getForward(transform);

        // Spotlight cone parameters
        float range = light.range;
        float outerAngle = glm::radians(light.outerCutoff);

        // Compute the cone base radius at maximum range
        float coneBaseRadius = range * glm::tan(outerAngle);

        // Vertices of the cone in local space
        glm::vec3 coneTip = position;
        glm::vec3 coneBaseCenter = position + direction * range;

        // Calculate orthogonal vectors to define the cone's circular base
        glm::vec3 right = glm::normalize(glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, direction));

        // Compute the base vertices of the cone
        std::vector<glm::vec3> vertices;
        for (int i = 0; i < 360; i += 90) {
            float angle = glm::radians(static_cast<float>(i));
            glm::vec3 offset = coneBaseRadius * (glm::cos(angle) * right + glm::sin(angle) * up);
            vertices.push_back(coneBaseCenter + offset);
        }

        // Add the cone tip
        vertices.push_back(coneTip);

        // Compute the AABB by finding min/max extents
        glm::vec3 minExtents = vertices[0];
        glm::vec3 maxExtents = vertices[0];

        for (const auto& vertex : vertices) {
            minExtents = glm::min(minExtents, vertex);
            maxExtents = glm::max(maxExtents, vertex);
        }

        // Set the AABB values
        worldAABB.center = (minExtents + maxExtents) * 0.5f;
        worldAABB.extents = (maxExtents - minExtents) * 0.5f;
    }
    

    void Scene::addLight(ECS::Light& light){
        AABB worldAABB{};
        if(light.type==LightType::POINT_LIGHT){ 
            createPointLightAABB(static_cast<PointLight&>(light),worldAABB);    
        }else if(light.type==LightType::SPOT_LIGHT){
            createSpotLightAABB(static_cast<SpotLight&>(light),worldAABB);
        }

        // Create light in octree and store reference
        auto* octreeObject = lightTree.createObject(&light, worldAABB);
        lightMap[&light] = octreeObject;
    }

    void Scene::removeRenderer(const Renderable& renderable){
        auto it = rendererMap.find(&renderable);
        if (it != rendererMap.end()) {
            rendererTree.removeObject(it->second);
            rendererMap.erase(it);
        }
    }

    void Scene::removeLight(const ECS::Light& light){
        auto it = lightMap.find(&light);
        if (it != lightMap.end()) {
            lightTree.removeObject(it->second);
            lightMap.erase(it);
        }
    }

    void Scene::updateRenderer(Renderable& renderable){
        // First get the existing octree object
        auto it = rendererMap.find(&renderable);
        if (it != rendererMap.end()) {
            // Calculate new bounds
            auto meshRenderer = renderable.meshRenderer;
            auto transform = renderable.transform;
            
            AABB worldAABB{};
            auto localBounds = meshRenderer.mesh->getLocalBounds();
            BoundingBoxSystem::getWorldBounds(worldAABB, localBounds, transform.modelMatrix);
            
            // Update the object in the octree
            rendererTree.updateObject(it->second, worldAABB);
        } else {
            // If not found, add it as new
            addRenderer(renderable);
        }
    }

    void Scene::updateLight(ECS::Light& light){
        // First get the existing octree object
        auto it = lightMap.find(&light);
        if (it != lightMap.end()) {
            // Calculate new bounds
            AABB worldAABB{};
            if (light.type == LightType::POINT_LIGHT) {
                createPointLightAABB(static_cast<PointLight&>(light), worldAABB);
            } else if (light.type == LightType::SPOT_LIGHT) {
                createSpotLightAABB(static_cast<SpotLight&>(light), worldAABB);
            }
            
            // Update the object in the octree
            lightTree.updateObject(it->second, worldAABB);
        } else {
            // If not found, add it as new
            addLight(light);
        }
    }

     AABB Scene::calculateSceneBounds() {
        // Start with a reasonably large default
        AABB bounds;
        bounds.center = glm::vec3(0.0f);
        bounds.extents = glm::vec3(1000.0f); // Large default size
        
        auto& ecsManager = ECS::ECSManager::getInstance();

        glm::vec3 minPoint(std::numeric_limits<float>::max());
        glm::vec3 maxPoint(std::numeric_limits<float>::lowest());
            
        ecsManager.forEachComponent<ECS::Renderable>([&](ECS::Renderable& renderable){
            auto transform = renderable.transform;
            auto meshRenderer = renderable.meshRenderer;
                
            AABB worldBounds;
            BoundingBoxSystem::getWorldBounds(worldBounds, 
                                            meshRenderer.mesh->getLocalBounds(),
                                            transform.modelMatrix);
                                                    
            minPoint = glm::min(minPoint, BoundingBoxSystem::getMin(worldBounds));
            maxPoint = glm::max(maxPoint, BoundingBoxSystem::getMax(worldBounds));
        });
            
            // If we found any valid bounds
        if (minPoint.x != std::numeric_limits<float>::max()) {
            bounds.center = (minPoint + maxPoint) * 0.5f;
            bounds.extents = (maxPoint - minPoint) * 0.5f;
                
            // Add some padding
            bounds.extents *= 1.5f;
        }
        
        
        return bounds;
    }

     Scene::Scene() 
        : rendererTree(calculateSceneBounds())
        , lightTree(calculateSceneBounds())
        ,environmentLighting{glm::vec3(0.0f), 0.0f, nullptr, 0.0f}
     {
     }

    void Scene::setEnvironmentLighting(const EnvironmentLighting* newEnvironmentLighting){
        environmentLighting = *newEnvironmentLighting;
    }

    std::vector<Renderable*> Scene::getVisibleRenderers(const ViewFrustum& frustum){
        return rendererTree.getVisibleObjects(frustum);
    }

    std::vector<ECS::Light*> Scene::getVisibleLights(const ViewFrustum& frustum){
        return lightTree.getVisibleObjects(frustum);
    }

    std::vector<Renderable*> Scene::getIntersectingRenderers(const AABB& bounds){
        return rendererTree.getIntersectingObjects(bounds);
    }

    std::vector<ECS::Light*> Scene::getIntersectingLights(const AABB& bounds){
        return lightTree.getIntersectingObjects(bounds);
    }

    void Scene::getVisibleBounds(const ViewFrustum& frustum,AABB& sceneBounds) {
        auto visibleRenderers = getVisibleRenderers(frustum);
        
        // Initialize with inverse limits
        glm::vec3 minPoint(std::numeric_limits<float>::max());
        glm::vec3 maxPoint(std::numeric_limits<float>::lowest());
        
        // If we have no visible objects, return a default small AABB
        if (visibleRenderers.empty()) {
            sceneBounds=AABB{glm::vec3(0.0f), glm::vec3(1.0f)};
        }
        
        // Expand the bounds to include all visible renderables
        for (auto* renderable : visibleRenderers) {
            auto it = rendererMap.find(renderable);
            if (it != rendererMap.end()) {
                const AABB& objectBounds = it->second->getBounds();
                
                glm::vec3 objMin = objectBounds.center - objectBounds.extents;
                glm::vec3 objMax = objectBounds.center + objectBounds.extents;
                
                minPoint = glm::min(minPoint, objMin);
                maxPoint = glm::max(maxPoint, objMax);
            }
        }
        
        // Create and return the encompassing AABB
        sceneBounds.center = (minPoint + maxPoint) * 0.5f;
        sceneBounds.extents = (maxPoint - minPoint) * 0.5f;
        
    }

}