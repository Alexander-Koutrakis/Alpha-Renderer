#include "camera_system.hpp"
#include "Engine/alpha_engine.hpp"



using namespace ECS;
namespace Systems {

void CameraSystem::run(Window& window) {
    auto& ecsManager = ECSManager::getInstance();
    auto* cameraPtr=ecsManager.getFirstComponent<Camera>();
    
    if (cameraPtr!=nullptr) {
        auto& camera=*cameraPtr;
        auto& transform = *ecsManager.getComponent<ECS::Transform>(camera.owner);

        if (window.wasWindowResized()) {
            float aspect = static_cast<float>(window.getExtent().width) / 
                      static_cast<float>(window.getExtent().height);
        
            camera.aspectRatio=aspect;
        }

        updateProjectionMatrix(camera);
        updateViewMatrix(transform, camera);
        updateViewProjectionMatrix(camera);
    }
}



void CameraSystem::updateViewProjectionMatrix(Camera& camera) {
    camera.viewProjectionMatrix = camera.projectionMatrix * camera.viewMatrix;
}

void CameraSystem::updateProjectionMatrix(Camera& camera) {
    camera.projectionMatrix = glm::perspectiveLH_ZO(
        camera.fov,
        camera.aspectRatio,
        camera.nearPlane,
        camera.farPlane
    );
    camera.projectionMatrix[1][1]*=-1.0f;
}

void CameraSystem::updateViewMatrix(const Transform& transform, Camera& camera) {
     camera.viewMatrix = glm::lookAtLH(
        transform.position,                    
        transform.position + TransformSystem::getForward(transform), 
        glm::vec3(0.0f, 1.0f, 0.0f)           
    );
}

glm::vec3 CameraSystem::getViewPosition(const Camera& camera)  {
    glm::mat3 rotMat(camera.viewMatrix);
    glm::vec3 d(camera.viewMatrix[3]);
    return -d * rotMat;
}

glm::vec3 CameraSystem::getViewDirection(const Camera& camera) {
    return -glm::vec3(camera.viewMatrix[0][2], camera.viewMatrix[1][2], camera.viewMatrix[2][2]);
}



void CameraSystem::setFieldOfView(float fovDegrees){
    auto& ecsManager = ECSManager::getInstance();
    auto& camera=*ecsManager.getFirstComponent<Camera>();
    camera.fov=fovDegrees;
   
}

void CameraSystem::setNearPlane(float nearPlane){
    auto& ecsManager = ECSManager::getInstance();
    auto& camera=*ecsManager.getFirstComponent<Camera>();
    camera.nearPlane=nearPlane;
    
}

void CameraSystem::setFarPlane(float farPlane){
    auto& ecsManager = ECSManager::getInstance();
    auto& camera=*ecsManager.getFirstComponent<Camera>();
    camera.farPlane=farPlane;
    
}

void CameraSystem::setAspectRatio(float ratio){
    auto& ecsManager = ECSManager::getInstance();
    auto& camera=*ecsManager.getFirstComponent<Camera>();
    camera.aspectRatio=ratio;
    
}

Math::ViewFrustum CameraSystem::createFrustumFromCamera(const ECS::Camera& camera) {
    auto frustum = Math::ViewFrustum::createFromViewProjection(
        camera.viewProjectionMatrix
    );
    return frustum;
}

}