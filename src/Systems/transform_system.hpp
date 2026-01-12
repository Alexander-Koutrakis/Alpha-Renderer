#pragma once

#include "core.hpp"
#include "ECS/components.hpp"
#include "ECS/ecs.hpp"
namespace Systems{
    class TransformSystem{
        public:
        static void updateTransform(ECS::Transform& transform);
        static void rotate(ECS::Transform& transform, float angle, const glm::vec3& axis);
        static void rotateRelative(ECS::Transform& transform, float yaw, float pitch, float roll);
        static glm::vec3 getRotationEuler(ECS::Transform& transform){return glm::eulerAngles(transform.rotation);}
        static void setRotationEuler(ECS::Transform& transform, const glm::vec3& eulerAngles){transform.rotation = glm::quat(eulerAngles);}
        static void setPosition(ECS::Transform& transform, const glm::vec3& position){transform.position = position;}
        static void setScale(ECS::Transform& transform, const glm::vec3& scale){transform.scale = scale;}
        static glm::vec3 getForward(const ECS::Transform& transform){return glm::rotate(transform.rotation, glm::vec3(0.0f, 0.0f, 1.0f));}
        static glm::vec3 getRight(const ECS::Transform& transform){return glm::rotate(transform.rotation, glm::vec3(1.0f, 0.0f, 0.0f));}
        static glm::vec3 getUp(const ECS::Transform& transform){return glm::rotate(transform.rotation, glm::vec3(0.0f, 1.0f, 0.0f));}
        private:
        static void updateModelMatrix(ECS::Transform& transform);
        static void updateNormalMatrix(ECS::Transform& transform);
        
    };
}

