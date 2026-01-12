#include "view_frustum.hpp"

#include <iostream>
namespace Math {

    ViewFrustum ViewFrustum::createPerspective(
    float fovY, 
    float aspectRatio, 
    float nearZ, 
    float farZ,
    const glm::mat4& viewMatrix) 
{
    ViewFrustum frustum;
    
    
    // Calculate view-projection matrix
    glm::mat4 projMatrix = glm::perspectiveLH_ZO(fovY, aspectRatio, nearZ, farZ);
    glm::mat4 vpMatrix = projMatrix * viewMatrix;

    // Extract frustum planes from VP matrix
    // Left plane
    frustum.planes[LEFT].normal[0] = vpMatrix[0][3] + vpMatrix[0][0];
    frustum.planes[LEFT].normal[1] = vpMatrix[1][3] + vpMatrix[1][0];
    frustum.planes[LEFT].normal[2] = vpMatrix[2][3] + vpMatrix[2][0];
    frustum.planes[LEFT].distance = vpMatrix[3][3] + vpMatrix[3][0];

    // Right plane
    frustum.planes[RIGHT].normal[0] = vpMatrix[0][3] - vpMatrix[0][0];
    frustum.planes[RIGHT].normal[1] = vpMatrix[1][3] - vpMatrix[1][0];
    frustum.planes[RIGHT].normal[2] = vpMatrix[2][3] - vpMatrix[2][0];
    frustum.planes[RIGHT].distance = vpMatrix[3][3] - vpMatrix[3][0];

    // Bottom plane
    frustum.planes[BOTTOM].normal[0] = vpMatrix[0][3] + vpMatrix[0][1];
    frustum.planes[BOTTOM].normal[1] = vpMatrix[1][3] + vpMatrix[1][1];
    frustum.planes[BOTTOM].normal[2] = vpMatrix[2][3] + vpMatrix[2][1];
    frustum.planes[BOTTOM].distance = vpMatrix[3][3] + vpMatrix[3][1];

    // Top plane
    frustum.planes[TOP].normal[0] = vpMatrix[0][3] - vpMatrix[0][1];
    frustum.planes[TOP].normal[1] = vpMatrix[1][3] - vpMatrix[1][1];
    frustum.planes[TOP].normal[2] = vpMatrix[2][3] - vpMatrix[2][1];
    frustum.planes[TOP].distance = vpMatrix[3][3] - vpMatrix[3][1];

    // Near plane (DirectX/Vulkan 0..1 clip space: use the third column only)
    frustum.planes[NEAR].normal[0] = vpMatrix[0][2];
    frustum.planes[NEAR].normal[1] = vpMatrix[1][2];
    frustum.planes[NEAR].normal[2] = vpMatrix[2][2];
    frustum.planes[NEAR].distance = vpMatrix[3][2];

    // Far plane
    frustum.planes[FAR].normal[0] = vpMatrix[0][3] - vpMatrix[0][2];
    frustum.planes[FAR].normal[1] = vpMatrix[1][3] - vpMatrix[1][2];
    frustum.planes[FAR].normal[2] = vpMatrix[2][3] - vpMatrix[2][2];
    frustum.planes[FAR].distance = vpMatrix[3][3] - vpMatrix[3][2];

    // Normalize all planes
    for (auto& plane : frustum.planes) {
        float length = glm::length(plane.normal);
        plane.normal /= length;
        plane.distance /= length;
    }

    return frustum;
}


ViewFrustum ViewFrustum::createPerspective(
    const glm::mat4& viewMatrix,
    const glm::mat4& projectionMatrix
    ) 
{
    ViewFrustum frustum;
    
    glm::mat4 vpMatrix = projectionMatrix * viewMatrix;

    // Extract frustum planes from VP matrix
    // Left plane
    frustum.planes[LEFT].normal[0] = vpMatrix[0][3] + vpMatrix[0][0];
    frustum.planes[LEFT].normal[1] = vpMatrix[1][3] + vpMatrix[1][0];
    frustum.planes[LEFT].normal[2] = vpMatrix[2][3] + vpMatrix[2][0];
    frustum.planes[LEFT].distance = vpMatrix[3][3] + vpMatrix[3][0];

    // Right plane
    frustum.planes[RIGHT].normal[0] = vpMatrix[0][3] - vpMatrix[0][0];
    frustum.planes[RIGHT].normal[1] = vpMatrix[1][3] - vpMatrix[1][0];
    frustum.planes[RIGHT].normal[2] = vpMatrix[2][3] - vpMatrix[2][0];
    frustum.planes[RIGHT].distance = vpMatrix[3][3] - vpMatrix[3][0];

    // Bottom plane
    frustum.planes[BOTTOM].normal[0] = vpMatrix[0][3] + vpMatrix[0][1];
    frustum.planes[BOTTOM].normal[1] = vpMatrix[1][3] + vpMatrix[1][1];
    frustum.planes[BOTTOM].normal[2] = vpMatrix[2][3] + vpMatrix[2][1];
    frustum.planes[BOTTOM].distance = vpMatrix[3][3] + vpMatrix[3][1];

    // Top plane
    frustum.planes[TOP].normal[0] = vpMatrix[0][3] - vpMatrix[0][1];
    frustum.planes[TOP].normal[1] = vpMatrix[1][3] - vpMatrix[1][1];
    frustum.planes[TOP].normal[2] = vpMatrix[2][3] - vpMatrix[2][1];
    frustum.planes[TOP].distance = vpMatrix[3][3] - vpMatrix[3][1];

    // Near plane (DirectX/Vulkan 0..1 clip space: use the third column only)
    frustum.planes[NEAR].normal[0] = vpMatrix[0][2];
    frustum.planes[NEAR].normal[1] = vpMatrix[1][2];
    frustum.planes[NEAR].normal[2] = vpMatrix[2][2];
    frustum.planes[NEAR].distance = vpMatrix[3][2];

    // Far plane
    frustum.planes[FAR].normal[0] = vpMatrix[0][3] - vpMatrix[0][2];
    frustum.planes[FAR].normal[1] = vpMatrix[1][3] - vpMatrix[1][2];
    frustum.planes[FAR].normal[2] = vpMatrix[2][3] - vpMatrix[2][2];
    frustum.planes[FAR].distance = vpMatrix[3][3] - vpMatrix[3][2];

    // Normalize all planes
    for (auto& plane : frustum.planes) {
        float length = glm::length(plane.normal);
        plane.normal /= length;
        plane.distance /= length;
    }

    return frustum;
}

ViewFrustum ViewFrustum::createFromViewProjection(
    const glm::mat4& viewProjectionMatrix
    ) 
{
    ViewFrustum frustum;

    // Extract frustum planes from VP matrix
    // Left plane
    frustum.planes[LEFT].normal[0] = viewProjectionMatrix[0][3] + viewProjectionMatrix[0][0];
    frustum.planes[LEFT].normal[1] = viewProjectionMatrix[1][3] + viewProjectionMatrix[1][0];
    frustum.planes[LEFT].normal[2] = viewProjectionMatrix[2][3] + viewProjectionMatrix[2][0];
    frustum.planes[LEFT].distance = viewProjectionMatrix[3][3] + viewProjectionMatrix[3][0];

    // Right plane
    frustum.planes[RIGHT].normal[0] = viewProjectionMatrix[0][3] - viewProjectionMatrix[0][0];
    frustum.planes[RIGHT].normal[1] = viewProjectionMatrix[1][3] - viewProjectionMatrix[1][0];
    frustum.planes[RIGHT].normal[2] = viewProjectionMatrix[2][3] - viewProjectionMatrix[2][0];
    frustum.planes[RIGHT].distance = viewProjectionMatrix[3][3] - viewProjectionMatrix[3][0];

    // Bottom plane
    frustum.planes[BOTTOM].normal[0] = viewProjectionMatrix[0][3] + viewProjectionMatrix[0][1];
    frustum.planes[BOTTOM].normal[1] = viewProjectionMatrix[1][3] + viewProjectionMatrix[1][1];
    frustum.planes[BOTTOM].normal[2] = viewProjectionMatrix[2][3] + viewProjectionMatrix[2][1];
    frustum.planes[BOTTOM].distance = viewProjectionMatrix[3][3] + viewProjectionMatrix[3][1];

    // Top plane
    frustum.planes[TOP].normal[0] = viewProjectionMatrix[0][3] - viewProjectionMatrix[0][1];
    frustum.planes[TOP].normal[1] = viewProjectionMatrix[1][3] - viewProjectionMatrix[1][1];
    frustum.planes[TOP].normal[2] = viewProjectionMatrix[2][3] - viewProjectionMatrix[2][1];
    frustum.planes[TOP].distance = viewProjectionMatrix[3][3] - viewProjectionMatrix[3][1];

    // Near plane (DirectX/Vulkan 0..1 clip space: use the third column only)
    frustum.planes[NEAR].normal[0] = viewProjectionMatrix[0][2];
    frustum.planes[NEAR].normal[1] = viewProjectionMatrix[1][2];
    frustum.planes[NEAR].normal[2] = viewProjectionMatrix[2][2];
    frustum.planes[NEAR].distance = viewProjectionMatrix[3][2];

    // Far plane
    frustum.planes[FAR].normal[0] = viewProjectionMatrix[0][3] - viewProjectionMatrix[0][2];
    frustum.planes[FAR].normal[1] = viewProjectionMatrix[1][3] - viewProjectionMatrix[1][2];
    frustum.planes[FAR].normal[2] = viewProjectionMatrix[2][3] - viewProjectionMatrix[2][2];
    frustum.planes[FAR].distance = viewProjectionMatrix[3][3] - viewProjectionMatrix[3][2];

    // Normalize all planes
    for (auto& plane : frustum.planes) {
        float length = glm::length(plane.normal);
        plane.normal /= length;
        plane.distance /= length;
    }

    return frustum;
}

    ViewFrustum ViewFrustum::createOrthographic(
        float left, float right,
        float bottom, float top,
        float nearZ, float farZ,
        const glm::mat4& viewMatrix
    ) {
        ViewFrustum frustum;
        glm::mat4 invView = glm::inverse(viewMatrix);

        // Calculate the eight corners of the frustum in world space
        std::array<glm::vec3, 8> corners;
        corners[0] = glm::vec3(left,  bottom, -nearZ); // near bottom left
        corners[1] = glm::vec3(right, bottom, -nearZ); // near bottom right
        corners[2] = glm::vec3(right, top,    -nearZ); // near top right
        corners[3] = glm::vec3(left,  top,    -nearZ); // near top left
        corners[4] = glm::vec3(left,  bottom, -farZ);  // far bottom left
        corners[5] = glm::vec3(right, bottom, -farZ);  // far bottom right
        corners[6] = glm::vec3(right, top,    -farZ);  // far top right
        corners[7] = glm::vec3(left,  top,    -farZ);  // far top left

        // Transform corners to world space
        for (auto& corner : corners) {
            glm::vec4 worldCorner = invView * glm::vec4(corner, 1.0f);
            corner = glm::vec3(worldCorner) / worldCorner.w;
        }

        // Calculate planes
        frustum.planes[LEFT] = Plane(
            glm::normalize(glm::cross(corners[7] - corners[4], corners[0] - corners[4])),
            -glm::dot(corners[4], glm::normalize(glm::cross(corners[7] - corners[4], corners[0] - corners[4])))
        );

        frustum.planes[RIGHT] = Plane(
            glm::normalize(glm::cross(corners[2] - corners[1], corners[6] - corners[1])),
            -glm::dot(corners[1], glm::normalize(glm::cross(corners[2] - corners[1], corners[6] - corners[1])))
        );

        frustum.planes[BOTTOM] = Plane(
            glm::normalize(glm::cross(corners[1] - corners[0], corners[4] - corners[0])),
            -glm::dot(corners[0], glm::normalize(glm::cross(corners[1] - corners[0], corners[4] - corners[0])))
        );

        frustum.planes[TOP] = Plane(
            glm::normalize(glm::cross(corners[7] - corners[3], corners[2] - corners[3])),
            -glm::dot(corners[3], glm::normalize(glm::cross(corners[7] - corners[3], corners[2] - corners[3])))
        );

        frustum.planes[NEAR] = Plane(
            glm::normalize(glm::cross(corners[2] - corners[0], corners[3] - corners[0])),
            -glm::dot(corners[0], glm::normalize(glm::cross(corners[2] - corners[0], corners[3] - corners[0])))
        );

        frustum.planes[FAR] = Plane(
            glm::normalize(glm::cross(corners[7] - corners[4], corners[6] - corners[4])),
            -glm::dot(corners[4], glm::normalize(glm::cross(corners[7] - corners[4], corners[6] - corners[4])))
        );

        return frustum;
    }

    void ViewFrustum::update(const glm::mat4& projView) {
        // Extract planes from projection-view matrix
        // Left plane
        planes[LEFT] = Plane(
            glm::vec3(projView[0][3] + projView[0][0],
                    projView[1][3] + projView[1][0],
                    projView[2][3] + projView[2][0]),
            projView[3][3] + projView[3][0]
        );

        // Right plane
        planes[RIGHT] = Plane(
            glm::vec3(projView[0][3] - projView[0][0],
                    projView[1][3] - projView[1][0],
                    projView[2][3] - projView[2][0]),
            projView[3][3] - projView[3][0]
        );

        // Bottom plane
        planes[BOTTOM] = Plane(
            glm::vec3(projView[0][3] + projView[0][1],
                    projView[1][3] + projView[1][1],
                    projView[2][3] + projView[2][1]),
            projView[3][3] + projView[3][1]
        );

        // Top plane
        planes[TOP] = Plane(
            glm::vec3(projView[0][3] - projView[0][1],
                    projView[1][3] - projView[1][1],
                    projView[2][3] - projView[2][1]),
            projView[3][3] - projView[3][1]
        );

    // Near plane 
    planes[NEAR] = Plane(
        glm::vec3(projView[0][2],
                projView[1][2],
                projView[2][2]),
        projView[3][2]
    );

        // Far plane
        planes[FAR] = Plane(
            glm::vec3(projView[0][3] - projView[0][2],
                    projView[1][3] - projView[1][2],
                    projView[2][3] - projView[2][2]),
            projView[3][3] - projView[3][2]
        );

        // Normalize all planes
        for (auto& plane : planes) {
            plane.normalize();
        }
    }

    ViewFrustum::Intersection ViewFrustum::testAABB(const AABB& aabb) const {
    bool allInside = true;
    
    // Get the AABB corners
    glm::vec3 center = aabb.center;
    glm::vec3 extents = aabb.extents;


    for (size_t i = 0; i < planes.size(); i++) {
        const auto& plane = planes[i];
        // Calculate the distance from the center to the plane
        float d = glm::dot(plane.normal, center) + plane.distance;
        
        // Calculate the radius in the direction of the normal
        float r = extents.x * std::abs(plane.normal.x) +
                 extents.y * std::abs(plane.normal.y) +
                 extents.z * std::abs(plane.normal.z);
        

        if (d < -r) {
            return Intersection::OUTSIDE;
        } else if (d < r) {
            // Box intersects this plane
            allInside = false;
        }
    }
    
    return allInside ? Intersection::INSIDE : Intersection::INTERSECT;
}

   

}