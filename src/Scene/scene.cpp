#include "Scene/scene.hpp"

using namespace Math;

using namespace ECS;
using namespace Systems;
namespace Scene {

void Scene::addRenderer(Renderable& renderable) {
    auto meshRenderer = renderable.meshRenderer;
    auto transform = renderable.transform;

    AABB worldAABB{};
    auto localBounds = meshRenderer.mesh->getLocalBounds();
    BoundingBoxSystem::getWorldBounds(worldAABB, localBounds, transform.modelMatrix);

    // Create object in the octree and store a reference to it
    auto* octreeObject = rendererTree.createObject(&renderable, worldAABB);
    rendererMap[&renderable] = octreeObject;
}

void Scene::createLightAABB(const ECS::Light& light, AABB& worldAABB) {
    if (light.type == LightType::POINT_LIGHT) {
        const auto& point = static_cast<const PointLight&>(light);
        BoundingBoxSystem::calculatePointLightBounds(worldAABB, point.transform.position, point.range);
    } else if (light.type == LightType::SPOT_LIGHT) {
        const auto& spot = static_cast<const SpotLight&>(light);
        BoundingBoxSystem::calculateSpotlightBounds(worldAABB, spot.transform.position,
                                                    TransformSystem::getForward(spot.transform), spot.range,
                                                    spot.outerCutoff);
    }
}

void Scene::addLight(ECS::Light& light) {
    AABB worldAABB{};
    createLightAABB(light, worldAABB);

    // Create light in octree and store reference
    auto* octreeObject = lightTree.createObject(&light, worldAABB);
    lightMap[&light] = octreeObject;
}

void Scene::removeRenderer(const Renderable& renderable) {
    auto it = rendererMap.find(&renderable);
    if (it != rendererMap.end()) {
        rendererTree.removeObject(it->second);
        rendererMap.erase(it);
    }
}

void Scene::removeLight(const ECS::Light& light) {
    auto it = lightMap.find(&light);
    if (it != lightMap.end()) {
        lightTree.removeObject(it->second);
        lightMap.erase(it);
    }
}

void Scene::updateRenderer(Renderable& renderable) {
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

void Scene::updateLight(ECS::Light& light) {
    // First get the existing octree object
    auto it = lightMap.find(&light);
    if (it != lightMap.end()) {
        // Calculate new bounds
        AABB worldAABB{};
        createLightAABB(light, worldAABB);

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

    ecsManager.forEachComponent<ECS::Renderable>([&](ECS::Renderable& renderable) {
        auto transform = renderable.transform;
        auto meshRenderer = renderable.meshRenderer;

        AABB worldBounds;
        BoundingBoxSystem::getWorldBounds(worldBounds, meshRenderer.mesh->getLocalBounds(), transform.modelMatrix);

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
    : rendererTree(calculateSceneBounds()),
      lightTree(calculateSceneBounds()),
      environmentLighting{glm::vec3(0.0f), 0.0f, nullptr, 0.0f} {}

void Scene::setEnvironmentLighting(const EnvironmentLighting* newEnvironmentLighting) {
    environmentLighting = *newEnvironmentLighting;
}

std::vector<Renderable*> Scene::getVisibleRenderers(const ViewFrustum& frustum) {
    return rendererTree.getVisibleObjects(frustum);
}

std::vector<ECS::Light*> Scene::getVisibleLights(const ViewFrustum& frustum) {
    return lightTree.getVisibleObjects(frustum);
}

std::vector<Renderable*> Scene::getIntersectingRenderers(const AABB& bounds) {
    return rendererTree.getIntersectingObjects(bounds);
}

std::vector<ECS::Light*> Scene::getIntersectingLights(const AABB& bounds) {
    return lightTree.getIntersectingObjects(bounds);
}

} // namespace Scene