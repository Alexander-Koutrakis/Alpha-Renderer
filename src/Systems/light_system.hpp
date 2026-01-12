#pragma once

#include "ECS/components.hpp"
#include "ECS/ecs_types.hpp"
#include "ECS/ecs.hpp"
#include "core.hpp"
#include "Systems/transform_system.hpp"
#include "Math/AABB.hpp"
#include "Math/view_frustum.hpp"
#include "Rendering/Core/frame_context.hpp"
#include "Scene/scene.hpp"
using namespace ECS;
using namespace Rendering;

namespace Systems{

    class LightSystem{
        public:
            
            static void updateFrameContext(FrameContext& frameContext);
        private:
            static void calculateCascadeSplits(DirectionalLight& directionalLight,float nearClip,float farClip);
            static void calculateCascadeViewProjections(
                DirectionalLight& directionalLight,
                CameraData& cameraData);
                
            static void frustumCullLights(
                    CameraData& cameraData, 
                    LightData& lightData);

            static void lightFrustumCullShadowCasters(
                LightData& lightData,
                ShadowcastingData& shadowcastingData,
                const CameraData& cameraData);        
            static void processDirectionalLightShadowCasters(
                    DirectionalLight& directionalLight,
                    ShadowcastingData& shadowcastingData,
                    Scene::Scene& scene,
                    const CameraData& cameraData);

            static void processSpotLightShadowCasters(
                    SpotLight& spotLight,
                    Rendering::ShadowcastingData& shadowcastingData,
                    Scene::Scene& scene,
                    const glm::vec3& cameraPosition);

            static void processPointLightShadowCasters(
                    PointLight& pointLight,
                    ShadowcastingData& shadowcastingData,
                    Scene::Scene& scene,
                    const glm::vec3& cameraPosition);
            
            static void updateDirectionalLight(
                DirectionalLight& directionalLight,
                const Transform& transform,
                CameraData& cameraData);
                        
            static void updatePointLight(PointLight& pointLight);
            static void updateSpotLight(SpotLight& spotLight);
            static void updateSceneLightBuffer(FrameContext& frameContext);    
            static void updateLightArrayBuffer(FrameContext& frameContext,LightData& lightData);
            static void updateCascadeSplitsBuffer(FrameContext& frameContext,LightData& lightData);
            static void updateShadowLightMatrixBuffer(FrameContext& frameContext,ShadowcastingData& shadowcastingData);
            static void updateShadowModelMatrixBuffer(FrameContext& frameContext,ShadowcastingData& shadowcastingData);
            static void updateShadowcastingData(FrameContext& frameContext,LightData& lightData);
    };
}