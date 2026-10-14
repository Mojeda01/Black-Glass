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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

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

        glfwSetWindowUserPointer(window.get(), &renderer);
        glfwSetFramebufferSizeCallback(window.get(),
            [](GLFWwindow* w, int, int) {
                auto* r = static_cast<AlethiaV3*>(glfwGetWindowUserPointer(w));
                if (r) r->setFramebufferResized();
            });
        
        glfwSetWindowFocusCallback(window.get(),
                [](GLFWwindow* w, int focused) {
                    auto* r = static_cast<AlethiaV3*>(glfwGetWindowUserPointer(w));
                    if (r && !focused) r->setMouseCaptured(false);
                });

        glfwSetMouseButtonCallback(window.get(),
                [](GLFWwindow* w, int button, int action, int mods) {
                    auto* r = static_cast<AlethiaV3*>(glfwGetWindowUserPointer(w));
                    if (!r)
                    {
                        return;
                    }
                    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
                    {
                        r->onLeftPress((mods & GLFW_MOD_SHIFT) != 0);
                    }
                    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
                    {
                        r->onLeftRelease();
                    }
                    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
                    {
                        r->onRightPress();
                    }
                    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
                    {
                        r->onRightRelease();
                    }
                });

        glfwSetScrollCallback(window.get(),
                [](GLFWwindow* w, double, double yoffset) {
                    auto* r = static_cast<AlethiaV3*>(glfwGetWindowUserPointer(w));
                    if (r) r->onScroll(yoffset);
                });

        glfwSetCursorPosCallback(window.get(),
                [](GLFWwindow* w, double xpos, double ypos) {
                    auto* r = static_cast<AlethiaV3*>(glfwGetWindowUserPointer(w));
                    if (r) r->onMouseMove(xpos, ypos);
                });

         glfwSetKeyCallback(window.get(),
                 [](GLFWwindow* w, int key, int, int action, int) {
                    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
                    {
                        glfwSetWindowShouldClose(w, GLFW_TRUE);
                    }

                    auto* r = static_cast<AlethiaV3*>(glfwGetWindowUserPointer(w));
                    if (r && action == GLFW_PRESS && key == GLFW_KEY_TAB)
                    {
                        r->toggleMouseCapture();
                        return;
                    }
                    if (r && action == GLFW_PRESS && key == GLFW_KEY_F1)
                    {
                        r->toggleUi();
                        return;
                    }
                    if (r && r->uiWantsKeyboard())
                    {
                        return;
                    }
                    if (r && action == GLFW_PRESS && (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE))
                    {
                        r->deleteSelected();
                        return;
                    }
                    if (r && action == GLFW_PRESS && key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
                    {
                        r->setDebugView(key - GLFW_KEY_0);
                    }
                    if (r && (action == GLFW_PRESS || action == GLFW_REPEAT))
                    {
                        if (key == GLFW_KEY_EQUAL) r->adjustExposure(0.25f);
                        if (key == GLFW_KEY_MINUS) r->adjustExposure(-0.25f);
                    }
                    if (r && action == GLFW_PRESS && key == GLFW_KEY_B)
                    {
                        r->toggleBloom();
                    }
                 });

        renderer.initImGui();

        while (!window.shouldClose()) {
            window.pollEvents();

            if (glfwGetWindowAttrib(window.get(), GLFW_ICONIFIED))
            {
                glfwWaitEvents();
                continue;
            }

            renderer.drawFrame();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
