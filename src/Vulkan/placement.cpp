#include "../BlackGlass.h"
#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace
{

bool intersectBox(const glm::vec3& origin, const glm::vec3& direction,
                  const glm::vec3& boxMin, const glm::vec3& boxMax, float& distance)
{
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; axis++)
    {
        if (std::abs(direction[axis]) < 1e-8f)
        {
            if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis])
            {
                return false;
            }
            continue;
        }
        float inv = 1.0f / direction[axis];
        float t0 = (boxMin[axis] - origin[axis]) * inv;
        float t1 = (boxMax[axis] - origin[axis]) * inv;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax)
        {
            return false;
        }
    }
    distance = tMin;
    return true;
}

bool intersectGround(const glm::vec3& origin, const glm::vec3& direction, float planeY, glm::vec3& hit)
{
    if (direction.y > -1e-5f)
    {
        return false;
    }
    float t = (planeY - origin.y) / direction.y;
    if (t <= 0.0f)
    {
        return false;
    }
    hit = origin + direction * t;
    return true;
}

}

bool BlackGlass::cursorRay(glm::vec3& origin, glm::vec3& direction) const
{
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(m_window, &windowWidth, &windowHeight);
    if (windowWidth <= 0 || windowHeight <= 0)
    {
        return false;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(m_window, &cursorX, &cursorY);

    float ndcX = static_cast<float>(cursorX / windowWidth) * 2.0f - 1.0f;
    float ndcY = static_cast<float>(cursorY / windowHeight) * 2.0f - 1.0f;

    glm::mat4 proj = glm::perspective(glm::radians(m_fov),
                                      M_SWAPCHAINEXTENT.width / static_cast<float>(M_SWAPCHAINEXTENT.height),
                                      0.1f, 1000.0f);
    proj[1][1] *= -1;
    glm::mat4 inverse = glm::inverse(proj * m_camera.view());

    glm::vec4 nearPoint = inverse * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = inverse * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    glm::vec3 nearWorld = glm::vec3(nearPoint) / nearPoint.w;
    glm::vec3 farWorld = glm::vec3(farPoint) / farPoint.w;

    origin = nearWorld;
    direction = glm::normalize(farWorld - nearWorld);
    return true;
}

Model* BlackGlass::pickModel(const glm::vec3& origin, const glm::vec3& direction, float& distance)
{
    Model* best = nullptr;
    float bestDistance = std::numeric_limits<float>::max();
    for (auto& model : M_MODELS)
    {
        if (!model->visible || model->ghost)
        {
            continue;
        }
        float t = 0.0f;
        if (intersectBox(origin, direction, model->worldMin, model->worldMax, t) && t < bestDistance)
        {
            bestDistance = t;
            best = model.get();
        }
    }
    distance = bestDistance;
    return best;
}

void BlackGlass::beginPlacement(int assetIndex)
{
    cancelPlacement();
    if (assetIndex < 0 || assetIndex >= static_cast<int>(M_LIBRARY.size()))
    {
        return;
    }

    m_ghost = addInstance(assetIndex, false);
    if (m_ghost)
    {
        m_ghost->ghost = true;
        m_ghost->visible = false;
        m_placingAsset = assetIndex;
        m_selected = nullptr;
    }
}

void BlackGlass::cancelPlacement()
{
    if (m_ghost)
    {
        for (size_t i = 0; i < M_MODELS.size(); i++)
        {
            if (M_MODELS[i].get() == m_ghost)
            {
                M_MODELS.erase(M_MODELS.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        m_ghost = nullptr;
    }
    m_placingAsset = -1;
}

void BlackGlass::updateInteraction()
{
    const bool cursorFree = !m_mouseCaptured && !uiWantsMouse();

    if (m_ghost)
    {
        m_ghost->visible = false;
        glm::vec3 origin;
        glm::vec3 direction;
        glm::vec3 hit;
        if (cursorFree && cursorRay(origin, direction) && intersectGround(origin, direction, 0.0f, hit))
        {
            m_ghost->position = glm::vec3(hit.x, 0.0f, hit.z);
            m_ghost->visible = true;
        }
        updateModelTransform(*m_ghost);
    }

    if (m_dragging && m_selected)
    {
        if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)
        {
            onLeftRelease();
            return;
        }

        glm::vec3 origin;
        glm::vec3 direction;
        glm::vec3 hit;
        if (cursorRay(origin, direction) && intersectGround(origin, direction, m_selected->position.y, hit))
        {
            m_selected->position.x = hit.x + m_dragOffset.x;
            m_selected->position.z = hit.z + m_dragOffset.y;
            updateModelTransform(*m_selected);
        }
    }
}

void BlackGlass::onLeftPress(bool shiftHeld)
{
    if (m_mouseCaptured || uiWantsMouse())
    {
        return;
    }

    if (m_ghost)
    {
        if (!m_ghost->visible)
        {
            return;
        }

        Model* placed = m_ghost;
        int assetIndex = m_placingAsset;
        placed->ghost = false;
        m_selected = placed;
        m_ghost = nullptr;
        m_placingAsset = -1;
        m_shadowDirty = true;

        if (shiftHeld)
        {
            beginPlacement(assetIndex);
            m_selected = placed;
        }
        return;
    }

    glm::vec3 origin;
    glm::vec3 direction;
    if (!cursorRay(origin, direction))
    {
        return;
    }

    float distance = 0.0f;
    m_selected = pickModel(origin, direction, distance);
    m_dragging = false;

    if (m_selected)
    {
        glm::vec3 hit;
        if (intersectGround(origin, direction, m_selected->position.y, hit))
        {
            m_dragOffset = glm::vec2(m_selected->position.x - hit.x, m_selected->position.z - hit.z);
            m_dragging = true;
        }
    }
}

void BlackGlass::onLeftRelease()
{
    if (m_dragging)
    {
        m_dragging = false;
        m_shadowDirty = true;
    }
}

void BlackGlass::onRightPress()
{
    if (!uiWantsMouse() && !m_mouseCaptured)
    {
        m_flyHeld = true;
        setMouseCaptured(true);
    }
}

void BlackGlass::onRightRelease()
{
    if (m_flyHeld)
    {
        m_flyHeld = false;
        setMouseCaptured(false);
    }
}

void BlackGlass::onScroll(double delta)
{
    if (uiWantsMouse())
    {
        return;
    }

    Model* target = m_ghost ? m_ghost : m_selected;
    if (!target)
    {
        return;
    }

    target->yawDegrees += static_cast<float>(delta) * 7.5f;
    if (target->yawDegrees > 180.0f)
    {
        target->yawDegrees -= 360.0f;
    }
    if (target->yawDegrees < -180.0f)
    {
        target->yawDegrees += 360.0f;
    }
    updateModelTransform(*target);
    if (!target->ghost)
    {
        m_shadowDirty = true;
    }
}

void BlackGlass::deleteSelected()
{
    if (m_ghost)
    {
        cancelPlacement();
        return;
    }

    if (!m_selected)
    {
        return;
    }

    for (size_t i = 0; i < M_MODELS.size(); i++)
    {
        if (M_MODELS[i].get() == m_selected)
        {
            removeModel(i);
            break;
        }
    }
}
