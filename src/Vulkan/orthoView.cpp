#include "../BlackGlass.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr float MIN_ORTHO_SIZE = 0.5f;
    constexpr float MAX_ORTHO_SIZE = 40.0f;
    constexpr float DEFAULT_ORTHO_SIZE = 10.0f;
}

float BlackGlass::activeFov() const
{
    if (m_orthoMode != 0)
    {
        return ORTHO_FOV;
    }
    return m_walkMode ? m_fov + m_walkFovBoost : m_fov;
}

float BlackGlass::orthoCameraDistance() const
{
    return m_orthoSize / std::tan(glm::radians(ORTHO_FOV) * 0.5f);
}

void BlackGlass::applyOrthoCamera()
{
    switch (m_orthoMode)
    {
    case 1:
        m_camera.yaw = -90.0f;
        m_camera.pitch = -89.9f;
        break;
    case 2:
        m_camera.yaw = -90.0f;
        m_camera.pitch = 0.0f;
        break;
    case 3:
        m_camera.yaw = 180.0f;
        m_camera.pitch = 0.0f;
        break;
    default:
        return;
    }
    m_camera.position = m_orthoTarget - m_camera.front() * orthoCameraDistance();
}

void BlackGlass::setOrthoView(int mode)
{
    mode = std::clamp(mode, 0, 3);
    if (mode == m_orthoMode)
    {
        return;
    }

    if (mode == 0)
    {
        m_orthoMode = 0;
        m_camera.position = m_orthoSavedPosition;
        m_camera.yaw = m_orthoSavedYaw;
        m_camera.pitch = m_orthoSavedPitch;
        return;
    }

    if (m_walkMode)
    {
        return;
    }

    if (m_orthoMode == 0)
    {
        m_orthoSavedPosition = m_camera.position;
        m_orthoSavedYaw = m_camera.yaw;
        m_orthoSavedPitch = m_camera.pitch;

        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        bool any = false;
        for (const Model* model : m_selection)
        {
            if (model->ghost || !model->asset)
            {
                continue;
            }
            lo = glm::min(lo, model->worldMin);
            hi = glm::max(hi, model->worldMax);
            any = true;
        }

        if (any)
        {
            m_orthoTarget = (lo + hi) * 0.5f;
            m_orthoSize = std::clamp(0.6f * glm::length(hi - lo), 4.0f, MAX_ORTHO_SIZE);
        }
        else
        {
            const glm::vec3 front = m_camera.front();
            glm::vec3 pivot = m_camera.position + front * 6.0f;
            if (front.y < -1e-4f)
            {
                float t = -m_camera.position.y / front.y;
                if (t > 0.0f && t < 100.0f)
                {
                    pivot = m_camera.position + front * t;
                    pivot.y = 0.0f;
                }
            }
            m_orthoTarget = pivot;
            m_orthoSize = DEFAULT_ORTHO_SIZE;
        }
    }

    m_orthoMode = mode;
    applyOrthoCamera();
}

void BlackGlass::panOrtho(float deltaX, float deltaY)
{
    int width = 0;
    int height = 0;
    glfwGetWindowSize(m_window, &width, &height);
    if (height <= 0)
    {
        return;
    }

    float scale = (2.0f * m_orthoSize) / static_cast<float>(height);
    glm::vec3 right = m_camera.right();
    glm::vec3 up = glm::cross(right, m_camera.front());
    m_orthoTarget += (-right * deltaX + up * deltaY) * scale;
    applyOrthoCamera();
}

void BlackGlass::zoomOrtho(float notches)
{
    m_orthoSize = std::clamp(m_orthoSize * std::pow(0.88f, notches), MIN_ORTHO_SIZE, MAX_ORTHO_SIZE);
    applyOrthoCamera();
}

void BlackGlass::fitOrtho(const glm::vec3& center, float radius)
{
    m_orthoTarget = center;
    m_orthoSize = std::clamp(radius * 1.2f, 1.0f, MAX_ORTHO_SIZE);
    applyOrthoCamera();
}

void BlackGlass::keyboardOrtho(float deltaTime)
{
    glm::vec3 right = m_camera.right();
    glm::vec3 up = glm::cross(right, m_camera.front());
    float step = m_orthoSize * 1.2f * deltaTime;

    glm::vec3 move(0.0f);
    if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) move += up;
    if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) move -= up;
    if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) move += right;
    if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) move -= right;

    if (glm::dot(move, move) > 0.0f)
    {
        m_orthoTarget += move * step;
        applyOrthoCamera();
    }
}
