#include "alpha_engine.hpp"
#include "Rendering/Core/vk_check.hpp"

#include <chrono>
#include <iostream>

// Define deltaTime here
float AlphaEngine::deltaTime = 0.0f;

void AlphaEngine::run() {
    init();
    auto currentTime = std::chrono::high_resolution_clock::now();
    deltaTime = 0.0f;

    // FPS calculation variables
    float fpsUpdateTimer = 0.0f;
    int frameCount = 0;
    float currentFPS = 0.0f;

    while (!window->shouldClose()) {
        glfwPollEvents();
        auto newTime = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
        currentTime = newTime;

        // Calculate FPS (update every 0.5 seconds for smooth display)
        frameCount++;
        fpsUpdateTimer += deltaTime;
        if (fpsUpdateTimer >= 0.5f) {
            currentFPS = frameCount / fpsUpdateTimer;
            frameCount = 0;
            fpsUpdateTimer = 0.0f;
        }

        // Pass frame stats to ImGui manager
        if (renderer->getImGuiManager()) {
            renderer->getImGuiManager()->setFrameStats(currentFPS, deltaTime * 1000.0f);
        }

        keyboardMovementSystem->run(deltaTime);
        Systems::CameraSystem::run(*window);
        renderer->run();
    }

    VK_CHECK(vkDeviceWaitIdle(device->getDevice()));
}

void AlphaEngine::init() {
    window = std::make_unique<Window>(WIDTH, HEIGHT, "Alpha Engine");
    device = std::make_unique<Device>(*window);
    resourceManager = std::make_unique<ResourceManager>(*device);

    loadScene();

    renderer = std::make_unique<Renderer>(*window, *device);

    keyboardMovementSystem = std::make_unique<KeyboardMovemenSystem>(window->getGLFWwindow());
}

void AlphaEngine::loadScene() {
    Resources::SceneLoader sceneLoader{*resourceManager, *device};
    sceneLoader.loadUnityScene("Assets/Scene/Scene.json");
}

AlphaEngine::~AlphaEngine() {
    if (device) {
        vkDeviceWaitIdle(device->getDevice());
    }

    // Destroy renderer and all its resources first
    renderer.reset();

    // Clean up resource manager
    if (resourceManager) {
        resourceManager->cleanup();
    }
    resourceManager.reset();

    // Destroy device last
    device.reset();
    window.reset();
}
