#pragma once

#include "core.hpp"
#include "ECS/components.hpp"
#include "ECS/ecs.hpp"
#include "Systems/transform_system.hpp"

namespace Systems {
    class KeyboardMovemenSystem {
    public:
        struct KeyMappings {
            int moveForward = GLFW_KEY_W;
            int moveBack = GLFW_KEY_S;
            int moveLeft = GLFW_KEY_A;
            int moveRight = GLFW_KEY_D;
            int moveUp = GLFW_KEY_E;
            int moveDown = GLFW_KEY_Q;
            int lookLeft = GLFW_KEY_LEFT;
            int lookRight = GLFW_KEY_RIGHT;
            int lookUp = GLFW_KEY_UP;
            int lookDown = GLFW_KEY_DOWN;
            int toggleCursor = GLFW_KEY_LEFT_ALT;
            int sprint = GLFW_KEY_LEFT_SHIFT;
        };

        KeyboardMovemenSystem(GLFWwindow* window);
        ~KeyboardMovemenSystem();

        void run(const float deltaTime);

    private:
        void handleMouseLook(ECS::Transform& transform, float dt);
        void handleArrowLook(ECS::Transform& transform, float dt);
        void handleKeyboardMovement(ECS::Transform& transform, float dt);
    
    
        GLFWwindow* window;
        KeyMappings keys{};
 
        // Movement settings
        float moveSpeed{10.0f};
        float arrowLookSpeed = 1.5f;
        float mouseSensitivity{5.0f};
        float sprintMultiplier{2.0f};

        // Mouse state
        double lastMouseX{0.0}, lastMouseY{0.0};
        bool firstMouse{true};
    };
}