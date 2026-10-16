#include "../BlackGlass.h"
#include "../Editor/Editor.h"
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

bool intersectVerticalPlane(const glm::vec3& origin, const glm::vec3& direction,
                            const glm::vec3& point, const glm::vec3& normal, float& hitY)
{
    float denominator = glm::dot(direction, normal);
    if (std::abs(denominator) < 1e-5f)
    {
        return false;
    }
    float t = glm::dot(point - origin, normal) / denominator;
    if (t <= 0.0f)
    {
        return false;
    }
    hitY = origin.y + direction.y * t;
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

    glm::mat4 proj = glm::perspective(glm::radians(activeFov()),
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
        if (!model->visible || model->ghost || model->locked)
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

bool BlackGlass::isSelected(const Model* model) const
{
    return std::find(m_selection.begin(), m_selection.end(), model) != m_selection.end();
}

void BlackGlass::clearSelection()
{
    m_selection.clear();
    m_selected = nullptr;
    m_dragging = false;
}

void BlackGlass::selectOnly(Model* model)
{
    m_selection.clear();
    m_selected = nullptr;
    addToSelection(model);
    expandGroups();
    if (model)
    {
        m_selected = model;
    }
}

void BlackGlass::expandGroups()
{
    Model* primary = m_selected;
    std::vector<int> groups;
    for (const Model* model : m_selection)
    {
        if (model->group != 0 && std::find(groups.begin(), groups.end(), model->group) == groups.end())
        {
            groups.push_back(model->group);
        }
    }
    if (groups.empty())
    {
        return;
    }
    for (const auto& model : M_MODELS)
    {
        if (model->ghost || model->isGround || !model->asset || model->group == 0)
        {
            continue;
        }
        if (std::find(groups.begin(), groups.end(), model->group) != groups.end())
        {
            addToSelection(model.get());
        }
    }
    if (primary)
    {
        m_selected = primary;
    }
}

int BlackGlass::allocateGroup()
{
    return ++m_groupCounter;
}

void BlackGlass::noteGroup(int group)
{
    m_groupCounter = std::max(m_groupCounter, group);
}

int BlackGlass::groupSelection()
{
    std::vector<Model*> members;
    for (Model* model : m_selection)
    {
        if (!model->ghost && !model->locked)
        {
            members.push_back(model);
        }
    }
    if (members.size() < 2)
    {
        return 0;
    }
    int id = allocateGroup();
    for (Model* model : members)
    {
        model->group = id;
    }
    return static_cast<int>(members.size());
}

int BlackGlass::ungroupSelection()
{
    int released = 0;
    for (Model* model : m_selection)
    {
        if (model->group != 0 && !model->locked)
        {
            model->group = 0;
            released++;
        }
    }
    return released;
}

void BlackGlass::addToSelection(Model* model)
{
    if (!model)
    {
        return;
    }
    if (!isSelected(model))
    {
        m_selection.push_back(model);
    }
    m_selected = model;
}

void BlackGlass::toggleSelection(Model* model)
{
    if (!model)
    {
        return;
    }
    auto found = std::find(m_selection.begin(), m_selection.end(), model);
    if (found == m_selection.end())
    {
        addToSelection(model);
        expandGroups();
        m_selected = model;
        return;
    }
    if (model->group != 0)
    {
        int group = model->group;
        m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                         [group](const Model* other) { return other->group == group; }),
                          m_selection.end());
        if (m_selected && m_selected->group == group)
        {
            m_selected = m_selection.empty() ? nullptr : m_selection.back();
        }
        return;
    }
    m_selection.erase(found);
    if (m_selected == model)
    {
        m_selected = m_selection.empty() ? nullptr : m_selection.back();
    }
}

void BlackGlass::rotateSelection(float degrees)
{
    std::vector<Model*> movable;
    for (Model* model : m_selection)
    {
        if (!model->locked)
        {
            movable.push_back(model);
        }
    }
    if (movable.empty())
    {
        return;
    }

    glm::vec2 center(0.0f);
    for (const Model* model : movable)
    {
        center += glm::vec2(model->position.x, model->position.z);
    }
    center /= static_cast<float>(movable.size());

    float radians = glm::radians(degrees);
    float cosine = std::cos(radians);
    float sine = std::sin(radians);
    bool orbit = movable.size() > 1;

    for (Model* model : movable)
    {
        if (orbit)
        {
            float dx = model->position.x - center.x;
            float dz = model->position.z - center.y;
            model->position.x = center.x + dx * cosine + dz * sine;
            model->position.z = center.y - dx * sine + dz * cosine;
        }

        model->yawDegrees += degrees;
        while (model->yawDegrees > 180.0f)
        {
            model->yawDegrees -= 360.0f;
        }
        while (model->yawDegrees < -180.0f)
        {
            model->yawDegrees += 360.0f;
        }
        updateModelTransform(*model);
    }
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
        clearSelection();
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
        if (m_dragVertical)
        {
            float hitY = 0.0f;
            if (cursorRay(origin, direction) &&
                intersectVerticalPlane(origin, direction, m_selected->position, m_dragPlaneNormal, hitY))
            {
                float target = hitY + m_dragHeightOffset;
                if (m_verticalSnap > 0.0f)
                {
                    target = std::round(target / m_verticalSnap) * m_verticalSnap;
                }
                float deltaY = target - m_selected->position.y;
                if (deltaY != 0.0f)
                {
                    for (Model* model : m_selection)
                    {
                        if (model->locked)
                        {
                            continue;
                        }
                        model->position.y += deltaY;
                        updateModelTransform(*model);
                    }
                }
            }
        }
        else if (cursorRay(origin, direction) && intersectGround(origin, direction, m_selected->position.y, hit))
        {
            float deltaX = hit.x + m_dragOffset.x - m_selected->position.x;
            float deltaZ = hit.z + m_dragOffset.y - m_selected->position.z;
            for (Model* model : m_selection)
            {
                if (model->locked)
                {
                    continue;
                }
                model->position.x += deltaX;
                model->position.z += deltaZ;
                updateModelTransform(*model);
            }
        }
    }
}

void BlackGlass::onLeftPress(bool shiftHeld, bool altHeld)
{
    if (m_mouseCaptured || uiWantsMouse())
    {
        return;
    }

    if (m_editor && m_editor->onLeftPress())
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
        selectOnly(placed);
        m_ghost = nullptr;
        m_placingAsset = -1;
        m_shadowDirty = true;

        if (shiftHeld)
        {
            beginPlacement(assetIndex);
            selectOnly(placed);
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
    Model* picked = pickModel(origin, direction, distance);
    m_dragging = false;

    if (shiftHeld)
    {
        toggleSelection(picked);
    }
    else if (!picked)
    {
        clearSelection();
    }
    else if (isSelected(picked))
    {
        m_selected = picked;
    }
    else
    {
        selectOnly(picked);
    }

    m_dragVertical = false;
    if (m_selected && picked && isSelected(picked) && altHeld && !shiftHeld)
    {
        glm::vec3 flat(origin.x - m_selected->position.x, 0.0f, origin.z - m_selected->position.z);
        m_dragPlaneNormal = glm::length(flat) > 1e-4f ? glm::normalize(flat) : glm::vec3(0.0f, 0.0f, 1.0f);
        float hitY = 0.0f;
        if (intersectVerticalPlane(origin, direction, m_selected->position, m_dragPlaneNormal, hitY))
        {
            m_dragHeightOffset = m_selected->position.y - hitY;
            m_dragVertical = true;
            m_dragging = true;
        }
    }
    else if (m_selected && picked && isSelected(picked))
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
    if (m_editor)
    {
        m_editor->onLeftRelease();
    }
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

    float degrees = static_cast<float>(delta) * 7.5f;

    if (m_ghost)
    {
        m_ghost->yawDegrees += degrees;
        if (m_ghost->yawDegrees > 180.0f)
        {
            m_ghost->yawDegrees -= 360.0f;
        }
        if (m_ghost->yawDegrees < -180.0f)
        {
            m_ghost->yawDegrees += 360.0f;
        }
        updateModelTransform(*m_ghost);
        return;
    }

    if (m_orthoMode != 0)
    {
        zoomOrtho(static_cast<float>(delta));
        return;
    }

    if (m_selection.empty())
    {
        return;
    }

    noteEditorInput();
    rotateSelection(degrees);
    m_shadowDirty = true;
}

void BlackGlass::noteEditorInput()
{
    if (m_editor)
    {
        m_editor->noteInput();
    }
}

void BlackGlass::deleteSelected()
{
    if (m_ghost)
    {
        cancelPlacement();
        return;
    }

    std::vector<Model*> doomed = m_selection;
    for (Model* target : doomed)
    {
        if (target->locked)
        {
            continue;
        }
        for (size_t i = 0; i < M_MODELS.size(); i++)
        {
            if (M_MODELS[i].get() == target)
            {
                removeModel(i);
                break;
            }
        }
    }
}
