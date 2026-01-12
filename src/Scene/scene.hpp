#pragma once

#include "Math/octree.hpp"
#include "ECS/ecs.hpp"
#include "ECS/components.hpp"
#include "Systems/bounding_box_system.hpp"
#include "enviroment_lighting.hpp"
#include <unordered_map>
#include "Systems/transform_system.hpp"
using EntityID = std::uint32_t;
using namespace ECS;

namespace Scene{

    class Scene{
        public:
            static Scene& getInstance(){
                static Scene instance{};
                return instance;
            }

            Scene(const Scene&) = delete;  
            Scene& operator=(const Scene&) = delete;
            Scene(Scene&&) = default;
            Scene& operator=(Scene&&) = default;

            void addRenderer(Renderable& renderable);
            void addLight(ECS::Light& light);
            void removeRenderer(const Renderable& renderable);
            void removeLight(const ECS::Light& light);
            void updateRenderer(ECS::Renderable& renderable);
            void updateLight(ECS::Light& light);
            void setEnvironmentLighting(const EnvironmentLighting* environmentLighting);
            
            std::vector<Renderable*> getVisibleRenderers(const ViewFrustum& frustum);
            std::vector<ECS::Light*> getVisibleLights(const ViewFrustum& frustum);
            std::vector<Renderable*> getIntersectingRenderers(const AABB& bounds);
            std::vector<ECS::Light*> getIntersectingLights(const AABB& bounds);
            const EnvironmentLighting& getEnvironmentLighting()const{return environmentLighting;}
            void getVisibleBounds(const ViewFrustum& frustum,AABB& sceneBounds);
        private:
            Scene();
            void createSpotLightAABB(SpotLight& light, AABB& worldAABB);
            void createPointLightAABB(PointLight& light, AABB& worldAABB);
            Octree<Renderable> rendererTree;
            Octree<ECS::Light> lightTree;
            std::unordered_map<const Renderable*, typename Octree<Renderable>::OctreeObject*> rendererMap{};
            std::unordered_map<const ECS::Light*, typename Octree<ECS::Light>::OctreeObject*> lightMap{};
            
            AABB calculateSceneBounds();
            EnvironmentLighting environmentLighting;
    };
}