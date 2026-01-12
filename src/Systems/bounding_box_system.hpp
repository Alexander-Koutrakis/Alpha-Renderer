#pragma once

//#define GLM_ENABLE_EXPERIMENTAL
//#include <glm/gtc/constants.hpp>
#include "core.hpp"
#include <limits>
#include <vector>
#include "Math/AABB.hpp"
using namespace Math;
namespace Systems{
    class BoundingBoxSystem {
    public:
        static void getWorldBounds(AABB& worldBounds,const AABB& localBounds, const glm::mat4& transform);
        static glm::vec3 getMin(const AABB& AABB);
        static glm::vec3 getMax(const AABB& AABB);
        static glm::vec3 getSize(const AABB& AABB);
        static bool Contains(const AABB& bounds, const glm::vec3& point);
        static void encapsulate(AABB& encapsulatedBounds,const std::vector<AABB>& AABBs);
        static bool intersects(const AABB& a, const AABB& b);

        static void calculatePointLightBounds(AABB&worldBounds,const glm::vec3& position, float range);

        static void calculateSpotlightBounds(AABB&worldBounds,const glm::vec3& position,const glm::vec3& direction,float range,float outerCutoffDegrees);

        // Returns true if the AABB overlaps the given camera-space depth range (left-handed, +Z forward)
        static bool overlapsViewDepthRange(const AABB& worldBounds, const glm::mat4& viewMatrix, float nearZ, float farZ);

    private:
        static void calculateSpotLightCorners(const glm::vec3& position,const glm::vec3& direction,float range,float outerCutoffRadians,std::vector<glm::vec3>& corners);
    };
}