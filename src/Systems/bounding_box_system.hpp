#pragma once

//#define GLM_ENABLE_EXPERIMENTAL
//#include <glm/gtc/constants.hpp>
#include "core.hpp"
#include <limits>
#include <vector>
#include "Math/AABB.hpp"
namespace Systems {
class BoundingBoxSystem {
public:
    static void getWorldBounds(Math::AABB& worldBounds, const Math::AABB& localBounds, const glm::mat4& transform);
    static glm::vec3 getMin(const Math::AABB& AABB);
    static glm::vec3 getMax(const Math::AABB& AABB);
    static glm::vec3 getSize(const Math::AABB& AABB);
    static bool Contains(const Math::AABB& bounds, const glm::vec3& point);
    static void encapsulate(Math::AABB& encapsulatedBounds, const std::vector<Math::AABB>& AABBs);
    static bool intersects(const Math::AABB& a, const Math::AABB& b);

    static void calculatePointLightBounds(Math::AABB& worldBounds, const glm::vec3& position, float range);

    static void calculateSpotlightBounds(Math::AABB& worldBounds, const glm::vec3& position, const glm::vec3& direction,
                                         float range, float outerCutoffDegrees);

    // Returns true if the AABB overlaps the given camera-space depth range (left-handed, +Z forward)
    static bool overlapsViewDepthRange(const Math::AABB& worldBounds, const glm::mat4& viewMatrix, float nearZ,
                                       float farZ);

private:
    static void calculateSpotLightCorners(const glm::vec3& position, const glm::vec3& direction, float range,
                                          float outerCutoffRadians, std::vector<glm::vec3>& corners);
};
} // namespace Systems