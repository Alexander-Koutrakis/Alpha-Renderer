#include "alpha_engine.hpp"
#include "Rendering/Core/vk_check.hpp"

#include "Engine/log.hpp"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>

using namespace Rendering;
using namespace Resources;
using namespace Systems;

// Define deltaTime here
float AlphaEngine::deltaTime = 0.0f;

namespace {

constexpr int MAX_CAPTURE_FRAME = 300;

// ALPHA_CAPTURE_FRAME=N (1 to MAX_CAPTURE_FRAME): after N rendered frames the engine logs "capture frame N reached" and stops
// rendering, so the window keeps showing exactly frame N. The screenshot, validation and RC comparison scripts wait for that
// line instead of sleeping for a number of seconds, which makes a run's length independent of the frame rate. 0 = off.
int captureFrameFromEnvironment() {
    const char* value = std::getenv("ALPHA_CAPTURE_FRAME");
    if (value == nullptr) {
        return 0;
    }
    int frame = 0;
    try {
        frame = std::stoi(value);
    } catch (const std::exception&) {
        frame = 0;
    }
    if (frame < 1 || frame > MAX_CAPTURE_FRAME) {
        throw std::runtime_error(std::string("ALPHA_CAPTURE_FRAME must be 1 to ") + std::to_string(MAX_CAPTURE_FRAME) +
                                 ", got: " + value);
    }
    return frame;
}

} // namespace

void AlphaEngine::run() {
    init();
    const int captureFrame = captureFrameFromEnvironment();
    int framesRendered = 0;
    auto currentTime = std::chrono::high_resolution_clock::now();
    deltaTime = 0.0f;

    // FPS calculation variables
    float fpsUpdateTimer = 0.0f;
    int frameCount = 0;
    float currentFPS = 0.0f;

    while (!window->shouldClose()) {
        glfwPollEvents();
        if (captureFrame > 0 && framesRendered >= captureFrame) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }
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
        framesRendered++;
        if (framesRendered == captureFrame) {
            Log::info("capture frame ", captureFrame, " reached");
        }
    }

    VK_CHECK(vkDeviceWaitIdle(device->getDevice()));
}

void AlphaEngine::init() {
    window = std::make_unique<Window>(WIDTH, HEIGHT, "Alpha Engine");
    device = std::make_unique<Device>(*window);
    resourceManager = std::make_unique<ResourceManager>(*device);

    loadScene();

    renderer = std::make_unique<Renderer>(*window, *device, resourceManager->getPBRDescriptorSetLayout());

    keyboardMovementSystem = std::make_unique<KeyboardMovementSystem>(window->getGLFWwindow());
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
