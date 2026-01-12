#pragma once

#include "core.hpp"
#include <array>
#include "AABB.hpp"
#include <iostream>

namespace Math {

    struct Plane {
        glm::vec3 normal;
        float distance;

        Plane() : normal(0.0f), distance(0.0f) {}
        Plane(const glm::vec3& n, float d) : normal(n), distance(d) {}

        void normalize() {
            float magnitude = glm::length(normal);
            normal /= magnitude;
            distance /= magnitude;
        }
    };

    class ViewFrustum {
    public:
        enum FrustumPlane {
            LEFT,
            RIGHT,
            BOTTOM,
            TOP,
            NEAR,
            FAR
        };

        // Constructor for perspective projection
        static ViewFrustum createPerspective(
            float fovY, 
            float aspectRatio, 
            float nearZ, 
            float farZ,
            const glm::mat4& viewMatrix
        );

        static ViewFrustum createPerspective(
            const glm::mat4& viewMatrix,
            const glm::mat4& projectionMatrix
        );

        static ViewFrustum createFromViewProjection(
            const glm::mat4& viewProjectionMatrix
        );


        // Constructor for orthographic projection
        static ViewFrustum createOrthographic(
            float left, float right,
            float bottom, float top,
            float nearZ, float farZ,
            const glm::mat4& viewMatrix
        );


        // Test if AABB is inside, outside, or intersecting frustum
        enum class Intersection {
            INSIDE,
            OUTSIDE,
            INTERSECT
        };
        ViewFrustum() = default;
        Intersection testAABB(const AABB& aabb) const;
        void update(const glm::mat4& projView);

    private:
        
        std::array<Plane, 6> planes;
    };

    

}