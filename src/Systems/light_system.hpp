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

namespace Systems {

class LightSystem {
public:
    static void updateFrameContext(Rendering::FrameContext& frameContext);

private:
    static void calculateCascadeSplits(ECS::DirectionalLight& directionalLight, float nearClip, float farClip);
    static void calculateCascadeViewProjections(ECS::DirectionalLight& directionalLight,
                                                Rendering::CameraData& cameraData);

    static void frustumCullLights(Rendering::CameraData& cameraData, Rendering::LightData& lightData);

    static void lightFrustumCullShadowCasters(Rendering::LightData& lightData,
                                              Rendering::ShadowcastingData& shadowcastingData,
                                              const Rendering::CameraData& cameraData);
    static void processDirectionalLightShadowCasters(ECS::DirectionalLight& directionalLight,
                                                     Rendering::ShadowcastingData& shadowcastingData,
                                                     Scene::Scene& scene, const Rendering::CameraData& cameraData);

    static void processSpotLightShadowCasters(ECS::SpotLight& spotLight,
                                              Rendering::ShadowcastingData& shadowcastingData, Scene::Scene& scene,
                                              const glm::vec3& cameraPosition);

    static void processPointLightShadowCasters(ECS::PointLight& pointLight,
                                               Rendering::ShadowcastingData& shadowcastingData, Scene::Scene& scene,
                                               const glm::vec3& cameraPosition);

    static void updateDirectionalLight(ECS::DirectionalLight& directionalLight, const ECS::Transform& transform,
                                       Rendering::CameraData& cameraData);

    static void updatePointLight(ECS::PointLight& pointLight);
    static void updateSpotLight(ECS::SpotLight& spotLight);
    static void updateSceneLightBuffer(Rendering::FrameContext& frameContext);
    static void updateLightArrayBuffer(Rendering::FrameContext& frameContext, Rendering::LightData& lightData);
    static void updateCascadeSplitsBuffer(Rendering::FrameContext& frameContext, Rendering::LightData& lightData);
    static void updateShadowLightMatrixBuffer(Rendering::FrameContext& frameContext,
                                              Rendering::ShadowcastingData& shadowcastingData);
    static void updateShadowModelMatrixBuffer(Rendering::FrameContext& frameContext,
                                              Rendering::ShadowcastingData& shadowcastingData);
    static void updateShadowcastingData(Rendering::FrameContext& frameContext, Rendering::LightData& lightData);
};
} // namespace Systems