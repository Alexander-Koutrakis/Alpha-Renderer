#pragma once
#include "core.hpp"
#include <functional>  // Add this for std::hash

namespace Math{
    struct AABB{
        glm::vec3 center;
        glm::vec3 extents;

        AABB(glm::vec3 center,glm::vec3 extents):center(center),extents(extents){};
        AABB(const glm::vec3 corners[8]) {fromCorners(corners);};
        AABB() = default;

        bool operator==(const AABB& other) const {
            return center == other.center && extents == other.extents;
        }
        static AABB fromViewProjection(const glm::mat4& viewProjMatrix);
        static AABB combineAABBs(const AABB& a, const AABB& b);
        private:
            void fromCorners(const glm::vec3 corners[8]);
    };
}


