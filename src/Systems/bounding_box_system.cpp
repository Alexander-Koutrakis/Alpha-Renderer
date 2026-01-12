#include "bounding_box_system.hpp"
#include <iostream>
namespace Systems{

 void BoundingBoxSystem::getWorldBounds(AABB& worldBounds,const AABB& localBounds, const glm::mat4& transform) {  
    // Get min/max corners
    glm::vec3 min = getMin(localBounds);
    glm::vec3 max = getMax(localBounds);
    
    // Calculate all 8 corners and transform them
    glm::vec3 worldMin(std::numeric_limits<float>::max());
    glm::vec3 worldMax(std::numeric_limits<float>::lowest());
    
    for(int i = 0; i < 8; i++) {
        glm::vec3 corner(
            (i & 1) ? max.x : min.x,
            (i & 2) ? max.y : min.y,
            (i & 4) ? max.z : min.z
        );
        
        glm::vec4 worldCorner = transform * glm::vec4(corner, 1.0f);
        worldMin = glm::min(worldMin, glm::vec3(worldCorner));
        worldMax = glm::max(worldMax, glm::vec3(worldCorner));
    }
    
    worldBounds.center = (worldMin + worldMax) * 0.5f;
    worldBounds.extents = (worldMax - worldMin) * 0.5f;
}


    glm::vec3 BoundingBoxSystem::getMin(const AABB& AABB) {
        return AABB.center - AABB.extents;
    }

    glm::vec3 BoundingBoxSystem::getMax(const AABB& AABB) {
        return AABB.center + AABB.extents;
    }

     glm::vec3 BoundingBoxSystem::getSize(const AABB& AABB) {
        return AABB.extents*2.0f;
    }

    bool BoundingBoxSystem::Contains(const AABB& AABB, const glm::vec3& point) {
        glm::vec3 min = getMin(AABB);
        glm::vec3 max = getMax(AABB);
        return (point.x >= min.x && point.x <= max.x &&
                point.y >= min.y && point.y <= max.y &&
                point.z >= min.z && point.z <= max.z);
    }

    void BoundingBoxSystem::encapsulate(AABB& encaspulationBounds,const std::vector<AABB>& boxes) {
      
       // Initialize with first box (like Unity does)
        encaspulationBounds = boxes[0];
        
        // Encapsulate remaining boxes
        for (size_t i = 1; i < boxes.size(); i++) {
            const auto& box = boxes[i];
            glm::vec3 minPoint = getMin(encaspulationBounds);
            glm::vec3 maxPoint = getMax(encaspulationBounds);
            
            // Expand bounds
            minPoint = glm::min(minPoint, getMin(box));
            maxPoint = glm::max(maxPoint, getMax(box));
            
            // Update encapsulated bounds
            encaspulationBounds.center = (minPoint + maxPoint) * 0.5f;
            encaspulationBounds.extents = (maxPoint - minPoint) * 0.5f;
        }
    }
    
    bool BoundingBoxSystem::intersects(const AABB& a, const AABB& b) {
        // Get the min and max points of both AABBs
        glm::vec3 aMin = getMin(a);
        glm::vec3 aMax = getMax(a);
        glm::vec3 bMin = getMin(b);
        glm::vec3 bMax = getMax(b);

        // Check for overlap in all three axes
        return (aMin.x <= bMax.x && aMax.x >= bMin.x) &&
            (aMin.y <= bMax.y && aMax.y >= bMin.y) &&
            (aMin.z <= bMax.z && aMax.z >= bMin.z);
    }

    void BoundingBoxSystem::calculatePointLightBounds(AABB&worldBounds,const glm::vec3& position, float range){
            worldBounds.extents=glm::vec3(range);
            worldBounds.center=position;
    }

    void BoundingBoxSystem::calculateSpotlightBounds(AABB&worldBounds,const glm::vec3& position,const glm::vec3& direction,float range,float outerCutoffDegrees) {
            std::vector<glm::vec3> corners;
            corners.reserve(5); // 4 corners of the cone base + light position

            float outerCutoffRadians = glm::radians(outerCutoffDegrees);
            calculateSpotLightCorners(position, direction, range, outerCutoffRadians, corners);

            // Find min and max points
            glm::vec3 minPoint(std::numeric_limits<float>::max());
            glm::vec3 maxPoint(std::numeric_limits<float>::lowest());

            for (const auto& corner : corners) {
                minPoint = glm::min(minPoint, corner);
                maxPoint = glm::max(maxPoint, corner);
            }

            // Calculate center and extents
            worldBounds.center = (minPoint + maxPoint) * 0.5f;
            worldBounds.extents = (maxPoint - minPoint) * 0.5f;
    }

    void BoundingBoxSystem::calculateSpotLightCorners(const glm::vec3& position,const glm::vec3& direction,float range,float outerCutoffRadians,std::vector<glm::vec3>& corners) {
        // Add light position as first corner
        corners.push_back(position);

        // Calculate the radius of the cone base
        float radius = range * std::tan(outerCutoffRadians);

        // Create a coordinate system where direction is the z-axis
        glm::vec3 normalizedDir = glm::normalize(direction);
        glm::vec3 right = glm::normalize(glm::cross(normalizedDir, glm::vec3(0, 1, 0)));
        if (glm::length(right) < 0.001f) {
            right = glm::normalize(glm::cross(normalizedDir, glm::vec3(1, 0, 0)));
        }
        glm::vec3 up = glm::normalize(glm::cross(right, normalizedDir));

        // Calculate the end position of the cone
        glm::vec3 endPosition = position + normalizedDir * range;

        // Add the four corners of the cone base
        corners.push_back(endPosition + right * radius + up * radius);
        corners.push_back(endPosition - right * radius + up * radius);
        corners.push_back(endPosition - right * radius - up * radius);
        corners.push_back(endPosition + right * radius - up * radius);
    }

    bool BoundingBoxSystem::overlapsViewDepthRange(const AABB& worldBounds, const glm::mat4& viewMatrix, float nearZ, float farZ) {
        glm::vec3 worldMin = getMin(worldBounds);
        glm::vec3 worldMax = getMax(worldBounds);

        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();
        for (int cornerIdx = 0; cornerIdx < 8; ++cornerIdx) {
            glm::vec3 corner(
                (cornerIdx & 1) ? worldMax.x : worldMin.x,
                (cornerIdx & 2) ? worldMax.y : worldMin.y,
                (cornerIdx & 4) ? worldMax.z : worldMin.z);
            float viewZ = (viewMatrix * glm::vec4(corner, 1.0f)).z;
            minZ = std::min(minZ, viewZ);
            maxZ = std::max(maxZ, viewZ);
        }

        return !(maxZ < nearZ || minZ > farZ);
    }

}