#pragma once

#include "Rendering/rendering_constants.hpp"
#include "Resources/scene_loader.hpp"
#include "Resources/resource_manager.hpp"
#include "Systems/camera_system.hpp"
#include "Systems/keyboard_movement_system.hpp"
#include "Systems/transform_system.hpp"
#include "Rendering/renderer.hpp"

#include <unordered_map>
#include <future>

using namespace Rendering;
using namespace Systems;
using namespace Resources;

    class AlphaEngine {
        using id_t = unsigned int;
    public:
        static constexpr int WIDTH = 1920;
        static constexpr int HEIGHT = 1080;
       
        AlphaEngine() = default;
        ~AlphaEngine();
        
        void run();
        
        static float getDeltaTime() {return deltaTime;}

    private:
        std::unique_ptr<Window> window;
        std::unique_ptr<Device> device;
        std::unique_ptr<Renderer> renderer;      
        std::unique_ptr<KeyboardMovemenSystem> keyboardMovementSystem;
        std::unique_ptr<ResourceManager> resourceManager;
        static float deltaTime;
        void init();
        void loadScene();
        std::future<bool> loadSceneAsync();
    };
