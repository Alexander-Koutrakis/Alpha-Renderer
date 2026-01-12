#include "keyboard_movement_system.hpp"
#include <algorithm>
#include <iostream>

using namespace ECS;
namespace Systems {

KeyboardMovemenSystem::KeyboardMovemenSystem(GLFWwindow* window) 
    : window{window} {}

KeyboardMovemenSystem::~KeyboardMovemenSystem(){
    std::cout << "KeyboardMovemenSystem destructor called" << std::endl;
}


void KeyboardMovemenSystem::run(const float deltaTime) {
    // Check for cursor toggle
    auto& ecsManager = ECSManager::getInstance(); 
    auto cameraEntity = ecsManager.getFirstComponent<Camera>();
    Transform& transform = *ecsManager.getComponent<Transform>(cameraEntity->owner);
    handleArrowLook(transform,deltaTime);    
    handleKeyboardMovement(transform, deltaTime);
    

    TransformSystem::updateTransform(transform);
}



void KeyboardMovemenSystem::handleMouseLook(Transform& transform, float dt) {
    // Get current mouse position
    double currentMouseX, currentMouseY;
    glfwGetCursorPos(window, &currentMouseX, &currentMouseY);

    if (firstMouse) {
        lastMouseX = currentMouseX;
        lastMouseY = currentMouseY;
        firstMouse = false;
        return;
    }

    // Calculate mouse movement
    float deltaX = static_cast<float>(currentMouseX - lastMouseX);
    float deltaY = static_cast<float>(currentMouseY - lastMouseY);

    // Update last mouse position
    lastMouseX = currentMouseX;
    lastMouseY = currentMouseY;

    // Convert to rotation values
    float yaw = -deltaX * mouseSensitivity * dt;
    float pitch = -deltaY * mouseSensitivity * dt;

    // First rotate around world up axis (yaw)
    glm::quat yawQuat = glm::angleAxis(yaw, glm::vec3(0.0f, -1.0f, 0.0f));
    transform.rotation = glm::normalize(yawQuat * transform.rotation);

    // Then rotate around local right axis (pitch)
    glm::vec3 rightAxis = TransformSystem::getRight(transform);
    glm::quat pitchQuat = glm::angleAxis(pitch, rightAxis);
    transform.rotation = glm::normalize(pitchQuat * transform.rotation);
}
void KeyboardMovemenSystem::handleArrowLook(Transform& transform, float dt) {
    float yaw = 0.0f;
    float pitch = 0.0f;

    if (glfwGetKey(window, keys.lookLeft) == GLFW_PRESS) {
        yaw -= arrowLookSpeed * dt;
    }
    if (glfwGetKey(window, keys.lookRight) == GLFW_PRESS) {
        yaw += arrowLookSpeed * dt;
    }
    if (glfwGetKey(window, keys.lookUp) == GLFW_PRESS) {
        pitch -= arrowLookSpeed * dt;
    }
    if (glfwGetKey(window, keys.lookDown) == GLFW_PRESS) {
        pitch += arrowLookSpeed * dt;
    }

    if (yaw != 0.0f || pitch != 0.0f) {
         // First rotate around world up axis (yaw)
        glm::quat yawQuat = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        transform.rotation = glm::normalize(yawQuat * transform.rotation);

        // Then rotate around local right axis (pitch)
        glm::vec3 rightAxis = TransformSystem::getRight(transform);
        glm::quat pitchQuat = glm::angleAxis(pitch, rightAxis);
        transform.rotation = glm::normalize(pitchQuat * transform.rotation);
    }
}
void KeyboardMovemenSystem::handleKeyboardMovement(ECS::Transform& transform, float dt) {
    glm::vec3 moveDir{0.0f};

    // Forward/Backward
    if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS) {
        moveDir += TransformSystem::getForward(transform);
    }
    if (glfwGetKey(window, keys.moveBack) == GLFW_PRESS) {
        moveDir -= TransformSystem::getForward(transform);
    }

    // Left/Right
    if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS) {
        moveDir += TransformSystem::getRight(transform);
    }
    if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS) {
        moveDir -= TransformSystem::getRight(transform);
    }

    // Up/Down
    if (glfwGetKey(window, keys.moveUp) == GLFW_PRESS) {
        moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
    }
    if (glfwGetKey(window, keys.moveDown) == GLFW_PRESS) {
        moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Apply movement if there is any input
    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        float currentSpeed = moveSpeed;
        
        // Apply sprint multiplier if sprint key is pressed
        if (glfwGetKey(window, keys.sprint) == GLFW_PRESS) {
            currentSpeed *= sprintMultiplier;
        }
        
        transform.position += moveDir * currentSpeed * dt;
    }
}



}