#include "../BlackGlass.h"
#include "../Editor/Editor.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float PLAYER_RADIUS = 0.3f;
    constexpr float PLAYER_HEIGHT = 1.8f;
    constexpr float EYE_HEIGHT = 1.65f;
    constexpr float STEP_HEIGHT = 0.5f;
    constexpr float GRAVITY = 22.0f;
    constexpr float JUMP_SPEED = 7.0f;
    constexpr float WALK_SPEED = 4.2f;
    constexpr float SPRINT_SPEED = 7.5f;
    constexpr float GROUND_ACCEL = 55.0f;
    constexpr float AIR_ACCEL = 14.0f;
    constexpr float PHYSICS_STEP = 1.0f / 120.0f;
    constexpr int MAX_PHYSICS_STEPS = 12;
    constexpr float EYE_SMOOTH_RATE = 16.0f;
    constexpr float EYE_SNAP_DISTANCE = 1.5f;
    constexpr float BOB_RADIANS_PER_METER = 2.1f;
    constexpr float BOB_SPRINT_STRIDE = 0.7f;
    constexpr float BOB_VERTICAL = 0.022f;
    constexpr float BOB_SIDE = 0.015f;
    constexpr float BOB_SKEW = 0.25f;
    constexpr float BOB_SMOOTH_TIME = 0.06f;
    constexpr float LAND_SMOOTH_TIME = 0.05f;
    constexpr float DT_SMOOTH_RATE = 0.12f;
    constexpr float DT_HITCH_RATIO = 3.0f;
    constexpr float LAND_MIN_IMPACT = 3.5f;
    constexpr float LAND_DIP_SCALE = 0.014f;
    constexpr float LAND_DIP_MAX = 0.2f;
    constexpr float LAND_RECOVER_RATE = 9.0f;
    constexpr float SPRINT_FOV_BOOST = 6.0f;
    constexpr float STRAFE_ROLL = 1.2f;
    constexpr float LOOK_ROLL_SCALE = 0.0025f;
    constexpr float MAX_ROLL = 2.0f;
    constexpr float FLOOR_HEIGHT = 0.0f;
    constexpr float FALL_LIMIT = -60.0f;
    constexpr int RESOLVE_PASSES = 4;
    constexpr float STEEP_SLOPE = 1.2f;

    void smoothDamp(float& value, float& velocity, float target, float smoothTime, float dt)
    {
        float omega = 2.0f / smoothTime;
        float x = omega * dt;
        float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        float change = value - target;
        float temp = (velocity + omega * change) * dt;
        velocity = (velocity - omega * temp) * decay;
        value = target + (change + temp) * decay;
    }

    glm::vec2 toLocal(const CollisionPiece& piece, const glm::vec2& world)
    {
        glm::vec2 d = world - piece.center;
        return glm::vec2(d.x * piece.cosYaw - d.y * piece.sinYaw,
                         d.x * piece.sinYaw + d.y * piece.cosYaw);
    }

    glm::vec2 toWorldVector(const CollisionPiece& piece, const glm::vec2& local)
    {
        return glm::vec2(local.x * piece.cosYaw + local.y * piece.sinYaw,
                         -local.x * piece.sinYaw + local.y * piece.cosYaw);
    }

    float topAt(const CollisionPiece& piece, float localZ)
    {
        float z = std::clamp(localZ, piece.minLocal.y, piece.maxLocal.y);
        return piece.baseY + piece.topOffset + piece.slope * (z - piece.minLocal.y);
    }

    bool insideRect(const CollisionPiece& piece, const glm::vec2& local)
    {
        return local.x >= piece.minLocal.x && local.x <= piece.maxLocal.x &&
               local.y >= piece.minLocal.y && local.y <= piece.maxLocal.y;
    }

    CollisionPiece makePiece(const glm::vec3& position, float yawDegrees, const glm::vec2& minLocal,
                             const glm::vec2& maxLocal, float topOffset, float slope)
    {
        CollisionPiece piece;
        piece.center = glm::vec2(position.x, position.z);
        float yaw = glm::radians(yawDegrees);
        piece.cosYaw = std::cos(yaw);
        piece.sinYaw = std::sin(yaw);
        piece.minLocal = minLocal;
        piece.maxLocal = maxLocal;
        piece.baseY = position.y;
        piece.topOffset = topOffset;
        piece.slope = slope;
        return piece;
    }
}

void BlackGlass::buildCollision()
{
    m_collision.clear();
    for (const auto& model : M_MODELS)
    {
        if (model->hidden || model->ghost || model->light || model->spawn || !model->visible)
        {
            continue;
        }

        if (!model->brush)
        {
            if (!model->solid || !model->asset)
            {
                continue;
            }
            glm::vec3 extent = (model->asset->localMax - model->asset->localMin) * model->scale;
            m_collision.push_back(makePiece(model->position, model->yawDegrees,
                                            glm::vec2(-extent.x * 0.5f, -extent.z * 0.5f),
                                            glm::vec2(extent.x * 0.5f, extent.z * 0.5f), extent.y, 0.0f));
            continue;
        }

        const BrushDef& brush = *model->brush;
        glm::vec3 size = brush.size * model->scale;
        float halfX = size.x * 0.5f;
        float halfZ = size.z * 0.5f;

        switch (brush.shape)
        {
        case BrushShape::Box:
            m_collision.push_back(makePiece(model->position, model->yawDegrees,
                                            glm::vec2(-halfX, -halfZ), glm::vec2(halfX, halfZ), size.y, 0.0f));
            break;
        case BrushShape::Ramp:
        {
            float slope = size.y / std::max(size.z, 1e-4f);
            CollisionPiece ramp = makePiece(model->position, model->yawDegrees,
                                            glm::vec2(-halfX, -halfZ), glm::vec2(halfX, halfZ), 0.0f, slope);
            ramp.steep = slope > STEEP_SLOPE;
            m_collision.push_back(ramp);
            break;
        }
        case BrushShape::Stairs:
        {
            int steps = std::clamp(brush.steps, 1, 64);
            float depth = size.z / static_cast<float>(steps);
            for (int i = 0; i < steps; i++)
            {
                float z0 = -halfZ + depth * static_cast<float>(i);
                float height = size.y * static_cast<float>(i + 1) / static_cast<float>(steps);
                m_collision.push_back(makePiece(model->position, model->yawDegrees,
                                                glm::vec2(-halfX, z0), glm::vec2(halfX, z0 + depth), height, 0.0f));
            }
            break;
        }
        case BrushShape::Wedge:
        {
            constexpr int SLICES = 8;
            float depth = size.z / static_cast<float>(SLICES);
            for (int i = 0; i < SLICES; i++)
            {
                float z0 = -halfZ + depth * static_cast<float>(i);
                float middle = (z0 + depth * 0.5f) / std::max(size.z, 1e-4f);
                float maxX = std::clamp(-middle, -0.5f, 0.5f) * size.x;
                m_collision.push_back(makePiece(model->position, model->yawDegrees,
                                                glm::vec2(-halfX, z0), glm::vec2(maxX, z0 + depth), size.y, 0.0f));
            }
            break;
        }
        case BrushShape::Cylinder:
        {
            constexpr int SLICES = 8;
            float depth = size.z / static_cast<float>(SLICES);
            for (int i = 0; i < SLICES; i++)
            {
                float z0 = -halfZ + depth * static_cast<float>(i);
                float middle = (z0 + depth * 0.5f) / std::max(halfZ, 1e-4f);
                float reach = halfX * std::sqrt(std::max(1.0f - middle * middle, 0.0f));
                m_collision.push_back(makePiece(model->position, model->yawDegrees,
                                                glm::vec2(-reach, z0), glm::vec2(reach, z0 + depth), size.y, 0.0f));
            }
            break;
        }
        }
    }
}

void BlackGlass::toggleWalkMode(bool fromCamera)
{
    if (m_walkMode)
    {
        exitWalkMode();
        return;
    }

    if (m_orthoMode != 0)
    {
        setOrthoView(0);
    }
    if (m_editor)
    {
        m_editor->cancelTool();
    }
    cancelPlacement();
    clearSelection();
    buildCollision();

    m_savedCameraPosition = m_camera.position;
    m_savedCameraYaw = m_camera.yaw;
    m_savedCameraPitch = m_camera.pitch;
    m_walkUiWasVisible = m_showUi;
    m_showUi = false;

    m_walkFeet = glm::vec3(m_camera.position.x, std::max(m_camera.position.y - EYE_HEIGHT, FLOOR_HEIGHT), m_camera.position.z);
    if (!fromCamera)
    {
        for (const auto& model : M_MODELS)
        {
            if (model->spawn && !model->hidden && !model->ghost)
            {
                m_walkFeet = model->position;
                m_camera.yaw = 90.0f - model->yawDegrees;
                m_camera.pitch = 0.0f;
                break;
            }
        }
    }
    m_walkStart = m_walkFeet;
    m_walkVelocity = glm::vec3(0.0f);
    m_walkGrounded = false;
    m_walkPrevFeet = m_walkFeet;
    m_walkAccumulator = 0.0f;
    m_walkEyeValid = false;
    m_walkBobPhase = 0.0f;
    m_walkBobAmount = 0.0f;
    m_walkLandImpact = 0.0f;
    m_walkLandOffset = 0.0f;
    m_walkFovBoost = 0.0f;
    m_walkRoll = 0.0f;
    m_walkSmoothDt = 0.0f;
    m_walkBobY = 0.0f;
    m_walkBobYVelocity = 0.0f;
    m_walkBobX = 0.0f;
    m_walkBobXVelocity = 0.0f;
    m_walkLandShown = 0.0f;
    m_walkLandVelocity = 0.0f;
    m_camera.roll = 0.0f;
    m_walkLastYaw = m_camera.yaw;
    m_camera.pitch = std::clamp(m_camera.pitch, -60.0f, 60.0f);
    m_walkMode = true;
    setMouseCaptured(true);
}

void BlackGlass::exitWalkMode()
{
    if (!m_walkMode)
    {
        return;
    }
    m_walkMode = false;
    m_collision.clear();
    m_camera.roll = 0.0f;
    m_walkFovBoost = 0.0f;
    m_camera.position = m_savedCameraPosition;
    m_camera.yaw = m_savedCameraYaw;
    m_camera.pitch = m_savedCameraPitch;
    m_showUi = m_walkUiWasVisible;
    setMouseCaptured(false);
}

void BlackGlass::updateWalk(float deltaTime)
{
    float rawTime = std::max(std::min(deltaTime, 0.1f), 1e-4f);
    if (m_walkSmoothDt <= 0.0f || rawTime > m_walkSmoothDt * DT_HITCH_RATIO)
    {
        m_walkSmoothDt = rawTime;
    }
    else
    {
        m_walkSmoothDt += (rawTime - m_walkSmoothDt) * DT_SMOOTH_RATE;
    }
    const float frameTime = m_walkSmoothDt;

    m_walkAccumulator += frameTime;

    int steps = 0;
    while (m_walkAccumulator >= PHYSICS_STEP && steps < MAX_PHYSICS_STEPS)
    {
        m_walkPrevFeet = m_walkFeet;
        stepWalk(PHYSICS_STEP);
        m_walkAccumulator -= PHYSICS_STEP;
        steps++;
    }
    if (steps == MAX_PHYSICS_STEPS)
    {
        m_walkAccumulator = 0.0f;
    }

    float alpha = std::clamp(m_walkAccumulator / PHYSICS_STEP, 0.0f, 1.0f);
    glm::vec3 feet = glm::mix(m_walkPrevFeet, m_walkFeet, alpha);
    float targetEye = feet.y + EYE_HEIGHT;

    if (!m_walkEyeValid || std::abs(targetEye - m_walkEyeY) > EYE_SNAP_DISTANCE)
    {
        m_walkEyeY = targetEye;
        m_walkEyeValid = true;
    }
    else if (m_walkGrounded)
    {
        float blend = 1.0f - std::exp(-EYE_SMOOTH_RATE * frameTime);
        m_walkEyeY += (targetEye - m_walkEyeY) * blend;
    }
    else
    {
        m_walkEyeY = targetEye;
    }

    glm::vec2 horizontal(m_walkVelocity.x, m_walkVelocity.z);
    float horizontalSpeed = glm::length(horizontal);
    float sprintFraction = std::clamp((horizontalSpeed - WALK_SPEED) / (SPRINT_SPEED - WALK_SPEED), 0.0f, 1.0f);

    float speedWeight = std::clamp((horizontalSpeed - 0.3f) / 1.2f, 0.0f, 1.0f);
    speedWeight = speedWeight * speedWeight * (3.0f - 2.0f * speedWeight);
    float bobTarget = m_walkGrounded ? speedWeight * std::clamp(horizontalSpeed / WALK_SPEED, 0.0f, 1.8f) : 0.0f;
    m_walkBobAmount += (bobTarget - m_walkBobAmount) * (1.0f - std::exp(-8.0f * frameTime));

    float stride = BOB_RADIANS_PER_METER * (1.0f + (BOB_SPRINT_STRIDE - 1.0f) * sprintFraction);
    m_walkBobPhase += horizontalSpeed * stride * frameTime;
    if (m_walkBobPhase > 6.2831853f * 64.0f)
    {
        m_walkBobPhase -= 6.2831853f * 64.0f;
    }

    float stepAngle = m_walkBobPhase * 2.0f;
    float bobShape = std::sin(stepAngle) - BOB_SKEW * std::sin(stepAngle * 2.0f);
    float bobVerticalTarget = bobShape * BOB_VERTICAL * m_walkBobAmount;
    float bobSideTarget = std::sin(m_walkBobPhase) * BOB_SIDE * m_walkBobAmount;
    smoothDamp(m_walkBobY, m_walkBobYVelocity, bobVerticalTarget, BOB_SMOOTH_TIME, frameTime);
    smoothDamp(m_walkBobX, m_walkBobXVelocity, bobSideTarget, BOB_SMOOTH_TIME, frameTime);

    if (m_walkLandImpact > 0.0f)
    {
        if (m_walkLandImpact > LAND_MIN_IMPACT)
        {
            float dip = std::min((m_walkLandImpact - LAND_MIN_IMPACT) * LAND_DIP_SCALE, LAND_DIP_MAX);
            m_walkLandOffset = std::max(m_walkLandOffset, dip);
        }
        m_walkLandImpact = 0.0f;
    }
    m_walkLandOffset *= std::exp(-LAND_RECOVER_RATE * frameTime);
    smoothDamp(m_walkLandShown, m_walkLandVelocity, m_walkLandOffset, LAND_SMOOTH_TIME, frameTime);

    float fovTarget = SPRINT_FOV_BOOST * sprintFraction;
    m_walkFovBoost += (fovTarget - m_walkFovBoost) * (1.0f - std::exp(-6.0f * frameTime));

    float yawRate = (m_camera.yaw - m_walkLastYaw) / rawTime;
    m_walkLastYaw = m_camera.yaw;
    float yawRadians = glm::radians(m_camera.yaw);
    glm::vec2 strafeAxis(-std::sin(yawRadians), std::cos(yawRadians));
    float strafeFraction = std::clamp(glm::dot(horizontal, strafeAxis) / SPRINT_SPEED, -1.0f, 1.0f);
    float rollTarget = std::clamp(strafeFraction * STRAFE_ROLL + yawRate * LOOK_ROLL_SCALE, -MAX_ROLL, MAX_ROLL);
    m_walkRoll += (rollTarget - m_walkRoll) * (1.0f - std::exp(-9.0f * frameTime));
    m_camera.roll = m_walkRoll;

    glm::vec3 sideAxis = m_camera.right();
    m_camera.position = glm::vec3(feet.x, m_walkEyeY + m_walkBobY - m_walkLandShown, feet.z) + sideAxis * m_walkBobX;
}

void BlackGlass::stepWalk(float dt)
{
    glm::vec3& feet = m_walkFeet;

    bool typing = m_imguiReady && ImGui::GetIO().WantTextInput;
    auto down = [&](int key) { return !typing && glfwGetKey(m_window, key) == GLFW_PRESS; };

    float yaw = glm::radians(m_camera.yaw);
    glm::vec2 forward(std::cos(yaw), std::sin(yaw));
    glm::vec2 right(-forward.y, forward.x);

    glm::vec2 wish(0.0f);
    if (down(GLFW_KEY_W)) wish += forward;
    if (down(GLFW_KEY_S)) wish -= forward;
    if (down(GLFW_KEY_D)) wish += right;
    if (down(GLFW_KEY_A)) wish -= right;
    if (glm::dot(wish, wish) > 1e-6f)
    {
        wish = glm::normalize(wish);
    }
    float speed = down(GLFW_KEY_LEFT_SHIFT) ? SPRINT_SPEED : WALK_SPEED;

    glm::vec2 velocity(m_walkVelocity.x, m_walkVelocity.z);
    glm::vec2 desired = wish * speed;
    glm::vec2 difference = desired - velocity;
    float differenceLength = glm::length(difference);
    float maxChange = (m_walkGrounded ? GROUND_ACCEL : AIR_ACCEL) * dt;
    if (differenceLength <= maxChange)
    {
        velocity = desired;
    }
    else
    {
        velocity += difference / differenceLength * maxChange;
    }

    glm::vec2 startPosition(feet.x, feet.z);
    glm::vec2 position = startPosition + velocity * dt;
    glm::vec2 intended = position - startPosition;

    for (int pass = 0; pass < RESOLVE_PASSES; pass++)
    {
        bool moved = false;
        for (const CollisionPiece& piece : m_collision)
        {
            if (piece.baseY >= feet.y + PLAYER_HEIGHT - 0.01f)
            {
                continue;
            }

            glm::vec2 local = toLocal(piece, position);
            glm::vec2 nearest = glm::clamp(local, piece.minLocal, piece.maxLocal);
            float blockTop = piece.steep ? piece.baseY + piece.topOffset + piece.slope * (piece.maxLocal.y - piece.minLocal.y)
                                         : topAt(piece, nearest.y);
            if (blockTop <= feet.y + STEP_HEIGHT)
            {
                continue;
            }

            glm::vec2 offset = local - nearest;
            float distance = glm::length(offset);
            glm::vec2 push(0.0f);
            if (distance > 1e-5f)
            {
                if (distance >= PLAYER_RADIUS)
                {
                    continue;
                }
                push = offset / distance * (PLAYER_RADIUS - distance);
            }
            else
            {
                float left = local.x - piece.minLocal.x;
                float rightGap = piece.maxLocal.x - local.x;
                float back = local.y - piece.minLocal.y;
                float front = piece.maxLocal.y - local.y;
                float best = std::min(std::min(left, rightGap), std::min(back, front));
                if (best == left) push = glm::vec2(-(left + PLAYER_RADIUS), 0.0f);
                else if (best == rightGap) push = glm::vec2(rightGap + PLAYER_RADIUS, 0.0f);
                else if (best == back) push = glm::vec2(0.0f, -(back + PLAYER_RADIUS));
                else push = glm::vec2(0.0f, front + PLAYER_RADIUS);
            }
            position += toWorldVector(piece, push);
            moved = true;
        }
        if (!moved)
        {
            break;
        }
    }
    glm::vec2 actual = position - startPosition;
    if (glm::dot(actual, actual) < glm::dot(intended, intended) - 1e-10f)
    {
        velocity = actual / dt;
    }
    m_walkVelocity.x = velocity.x;
    m_walkVelocity.z = velocity.y;
    feet.x = position.x;
    feet.z = position.y;

    auto groundAt = [&](float referenceFeet) {
        float ground = FLOOR_HEIGHT;
        const float reach = PLAYER_RADIUS * 0.8f;
        const glm::vec2 samples[5] = { { 0.0f, 0.0f }, { reach, 0.0f }, { -reach, 0.0f }, { 0.0f, reach }, { 0.0f, -reach } };
        for (const CollisionPiece& piece : m_collision)
        {
            for (const glm::vec2& sample : samples)
            {
                glm::vec2 local = toLocal(piece, position + sample);
                if (!insideRect(piece, local))
                {
                    continue;
                }
                float top = topAt(piece, local.y);
                if (top <= referenceFeet + STEP_HEIGHT + 1e-3f)
                {
                    ground = std::max(ground, top);
                }
            }
        }
        return ground;
    };

    float ground = groundAt(feet.y);
    if (m_walkGrounded && m_walkVelocity.y <= 0.0f && feet.y - ground <= STEP_HEIGHT + 0.01f)
    {
        feet.y = ground;
        m_walkVelocity.y = 0.0f;
        if (down(GLFW_KEY_SPACE))
        {
            m_walkVelocity.y = JUMP_SPEED;
            m_walkGrounded = false;
        }
    }
    else
    {
        m_walkGrounded = false;
        m_walkVelocity.y -= GRAVITY * dt;
        float next = feet.y + m_walkVelocity.y * dt;

        if (m_walkVelocity.y > 0.0f)
        {
            for (const CollisionPiece& piece : m_collision)
            {
                if (piece.baseY < feet.y + PLAYER_HEIGHT - 0.01f || piece.baseY >= next + PLAYER_HEIGHT)
                {
                    continue;
                }
                glm::vec2 local = toLocal(piece, position);
                glm::vec2 nearest = glm::clamp(local, piece.minLocal, piece.maxLocal);
                if (glm::length(local - nearest) < PLAYER_RADIUS)
                {
                    next = piece.baseY - PLAYER_HEIGHT;
                    m_walkVelocity.y = 0.0f;
                }
            }
        }

        if (next <= ground && m_walkVelocity.y <= 0.0f)
        {
            m_walkLandImpact = std::max(m_walkLandImpact, -m_walkVelocity.y);
            feet.y = ground;
            m_walkVelocity.y = 0.0f;
            m_walkGrounded = true;
        }
        else
        {
            feet.y = next;
        }
    }

    if (feet.y < FALL_LIMIT)
    {
        feet = m_walkStart;
        m_walkVelocity = glm::vec3(0.0f);
        m_walkGrounded = false;
        m_walkPrevFeet = feet;
        m_walkEyeValid = false;
    }
}
