#include "transform_system.hpp"

namespace Systems{
    void TransformSystem::updateTransform(ECS::Transform& transform){
        updateModelMatrix(transform);
        updateNormalMatrix(transform);
    }

    void TransformSystem::updateModelMatrix(ECS::Transform& transform){
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.position);
        glm::mat4 rotation_mat = glm::mat4_cast(transform.rotation);
        glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), transform.scale);
        
        transform.modelMatrix = translation * rotation_mat * scale_mat;
        
    }

    void TransformSystem::updateNormalMatrix(ECS::Transform& transform){
        // Normal matrix = transpose(inverse(upper3x3(modelMatrix)))
        glm::mat3 modelMat3 = glm::mat3(transform.modelMatrix);
        glm::mat3 normal_mat3 = glm::transpose(glm::inverse(modelMat3));

        // Convert to mat4
        glm::mat4 normal_mat4(1.0f);
        normal_mat4[0] = glm::vec4(normal_mat3[0], 0.0f);
        normal_mat4[1] = glm::vec4(normal_mat3[1], 0.0f);
        normal_mat4[2] = glm::vec4(normal_mat3[2], 0.0f);
        normal_mat4[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        transform.normalMatrix = normal_mat4;
    }

    // Rotate by an angle around an axis
    void TransformSystem::rotate(ECS::Transform& transform, float angle, const glm::vec3& axis) {
        glm::quat rotationDelta = glm::angleAxis(angle, glm::normalize(axis));
        transform.rotation = rotationDelta * transform.rotation;
        transform.rotation = glm::normalize(transform.rotation); // Prevent any floating-point drift

        updateTransform(transform);
    }

    void TransformSystem::rotateRelative(ECS::Transform& transform, float yaw, float pitch, float roll) {
        glm::quat quatYaw = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat quatPitch = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat quatRoll = glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f)); 
        
        transform.rotation = quatYaw * quatPitch * quatRoll * transform.rotation;
        transform.rotation = glm::normalize(transform.rotation);

        updateTransform(transform);
    }





}
