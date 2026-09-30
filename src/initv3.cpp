#include <iostream>
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <GLFW/glfw3.h>

#include "AlethiaV3.h"

class V3Window {
public:
    V3Window(int width = 1280, int height = 720,
             const char* title = "Alethia v3");
    ~V3Window();

    GLFWwindow* get() const { return m_window; }
    bool shouldClose() const;
    void pollEvents();

private:
    GLFWwindow* m_window = nullptr;
    int m_width;
    int m_height;
};

V3Window::V3Window(int width, int height, const char* title)
    : m_width(width), m_height(height)
{
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

}

V3Window::~V3Window() {
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool V3Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void V3Window::pollEvents() {
    glfwPollEvents();
}

void initv3() {
    std::cout << "Alethia v3 initialization started.\n";

    try {
        V3Window window(1280, 720, "Alethia v3");
        std::cout << "GLFW window created successfully.\n";

        AlethiaV3 renderer(window.get());
        renderer.initVulkan();

        while (!window.shouldClose()) {
            window.pollEvents();
            renderer.drawFrame();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
