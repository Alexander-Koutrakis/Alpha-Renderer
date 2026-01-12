#pragma once
#include "ECS/components.hpp"
#include "ECS/ecs.hpp"
#include "Rendering/Core/window.hpp"
#include <array>
#include "Math/view_frustum.hpp"
#include "Math/octree.hpp"
#include "Scene/scene.hpp"
#include "Systems/transform_system.hpp"
#include "core.hpp"
namespace Systems {
    class CameraSystem {
    public:

        static void run(Window& window);

        // Camera settings
        static void setFieldOfView(float fovDegrees);
        static void setNearPlane(float near);
        static void setFarPlane(float far);
        static void setAspectRatio(float ratio);

        // Camera utility functions (moved from Camera component)
        static glm::vec3 getViewPosition(const ECS::Camera& camera);
        static glm::vec3 getViewDirection(const ECS::Camera& camera);


        static Math::ViewFrustum createFrustumFromCamera(const ECS::Camera& camera);
    private:
        static void updateProjectionMatrix(ECS::Camera& camera);
        static void updateViewMatrix(const ECS::Transform& transform, ECS::Camera& camera);
        static void updateViewProjectionMatrix(ECS::Camera& camera);
    };
}