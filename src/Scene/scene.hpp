#pragma once

#include "Math/octree.hpp"
#include "ECS/ecs.hpp"
#include "ECS/components.hpp"
#include "Systems/bounding_box_system.hpp"
#include "enviroment_lighting.hpp"
#include <unordered_map>
#include "Systems/transform_system.hpp"
using EntityID = std::uint32_t;

namespace Scene {

class Scene {
public:
    static Scene& getInstance() {
        static Scene instance{};
        return instance;
    }

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;

    void addRenderer(ECS::Renderable& renderable);
    void addLight(ECS::Light& light);
    void removeRenderer(const ECS::Renderable& renderable);
    void removeLight(const ECS::Light& light);
    void updateRenderer(ECS::Renderable& renderable);
    void updateLight(ECS::Light& light);
    void setEnvironmentLighting(const EnvironmentLighting* environmentLighting);

    std::vector<ECS::Renderable*> getVisibleRenderers(const Math::ViewFrustum& frustum);
    std::vector<ECS::Light*> getVisibleLights(const Math::ViewFrustum& frustum);
    std::vector<ECS::Renderable*> getIntersectingRenderers(const Math::AABB& bounds);
    std::vector<ECS::Light*> getIntersectingLights(const Math::AABB& bounds);
    const EnvironmentLighting& getEnvironmentLighting() const { return environmentLighting; }

private:
    Scene();
    void createLightAABB(const ECS::Light& light, Math::AABB& worldAABB);
    Math::Octree<ECS::Renderable> rendererTree;
    Math::Octree<ECS::Light> lightTree;
    std::unordered_map<const ECS::Renderable*, typename Math::Octree<ECS::Renderable>::OctreeObject*> rendererMap{};
    std::unordered_map<const ECS::Light*, typename Math::Octree<ECS::Light>::OctreeObject*> lightMap{};

    Math::AABB calculateSceneBounds();
    EnvironmentLighting environmentLighting;
};
} // namespace Scene