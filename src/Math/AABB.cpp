#include "AABB.hpp"

namespace Math{

    void AABB::fromCorners(const glm::vec3 corners[8]) {
        glm::vec3 minCorner(std::numeric_limits<float>::max());
        glm::vec3 maxCorner(std::numeric_limits<float>::lowest());

        for(int i = 0; i < 8; i++) {
            minCorner = glm::min(minCorner, corners[i]);
            maxCorner = glm::max(maxCorner, corners[i]);
        }

        center = (minCorner + maxCorner) * 0.5f;
        extents = (maxCorner - minCorner) * 0.5f;
    }

    AABB AABB::fromViewProjection(const glm::mat4& viewProjMatrix) {
        // NDC cube corners (normalized device coordinates)
        glm::vec4 ndcCorners[8] = {
            glm::vec4(-1, -1, 0, 1), glm::vec4(1, -1, 0, 1),  // near bottom-left/right
            glm::vec4(-1,  1, 0, 1), glm::vec4(1,  1, 0, 1),  // near top-left/right
            glm::vec4(-1, -1, 1, 1), glm::vec4(1, -1, 1, 1),  // far bottom-left/right
            glm::vec4(-1,  1, 1, 1), glm::vec4(1,  1, 1, 1)   // far top-left/right
        };
        
        // Inverse view-projection matrix
        glm::mat4 invViewProj = glm::inverse(viewProjMatrix);
        
        // Transform NDC corners to world space
        glm::vec3 worldCorners[8];
        for (int i = 0; i < 8; i++) {
            glm::vec4 worldPos = invViewProj * ndcCorners[i];
            worldCorners[i] = glm::vec3(worldPos) / worldPos.w;  // Perspective divide
        }
        
        // Create AABB from world corners
        return AABB(worldCorners);
    }

    AABB AABB::combineAABBs(const AABB& a, const AABB& b) {
        glm::vec3 minA = a.center - a.extents;
        glm::vec3 maxA = a.center + a.extents;
        glm::vec3 minB = b.center - b.extents;
        glm::vec3 maxB = b.center + b.extents;
        
        glm::vec3 combinedMin = glm::min(minA, minB);
        glm::vec3 combinedMax = glm::max(maxA, maxB);
        
        return AABB{(combinedMin + combinedMax) * 0.5f, (combinedMax - combinedMin) * 0.5f};
    }

}
