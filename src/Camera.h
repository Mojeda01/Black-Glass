
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera 
{
public:
    glm::vec3 position{ 0.0f, 0.0f, 3.0f };
    float yaw = -90.0f;
    float pitch = 0.0f;
    float speed = 2.5f;
    float sensitivity = 0.1f;

    glm::vec3 front() const 
    {
        glm::vec3 f;
        f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        f.y = sin(glm::radians(pitch));
        f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::normalize(f);
    }

    glm::vec3 right() const 
    {
        return glm::normalize(glm::cross(front(), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    glm::mat4 view() const
    {
        return glm::lookAt(position, position + front(), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void processMouse(float deltaX, float deltaY)
    {
        yaw += deltaX * sensitivity;
        pitch -= deltaY * sensitivity;

        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }

    void processKeyboard(GLFWwindow* window, float deltaTime)
    {
        float velocity = speed * deltaTime;
        glm::vec3 up{ 0.0f, 1.0f, 0.0f };

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position += front() * velocity;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position -= front() * velocity;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) position -= right() * velocity;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) position += right() * velocity;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) position += up * velocity;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) position -= up * velocity;
    }
};
