// std
#include <stdexcept>

#include "window.hpp"


namespace Rendering {

    Window::Window(int w, int h, std::string name) : width{ w }, height{ h }, windowName{ name } {
        initWindow();
    }

    Window::~Window() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void Window::initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        
        // Use borderless fullscreen (windowed fullscreen) instead of exclusive fullscreen
        // This prevents Windows from minimizing the window when system tools are activated
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        window = glfwCreateWindow(mode->width, mode->height, windowName.c_str(), nullptr, nullptr);
        glfwSetWindowPos(window, 0, 0);
        
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        
        width = mode->width;
        height = mode->height;
    }

    void Window::createWindowSurface(VkInstance instance, VkSurfaceKHR* surface) {
        if (glfwCreateWindowSurface(instance, window, nullptr, surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to craete window surface");
        }
    }

    void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto aWindow = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
        // Only mark as resized if window is not minimized (width/height > 0)
        if (width > 0 && height > 0) {
            aWindow->framebufferResized = true;
            aWindow->width = width;
            aWindow->height = height;
        } else {
            // Window is minimized, don't update dimensions but mark as resized
            // so we can skip rendering
            aWindow->framebufferResized = true;
        }
    }

} 