#include "../BlackGlass.h"
#include "../Editor/Editor.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

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
    constexpr float AIR_STEP_HEIGHT = 0.1f;
    constexpr float NEAR_MARGIN = 1.0f;
    constexpr float PUSH_EPSILON = 1e-4f;

    constexpr float GRID_CELL = 4.0f;
    constexpr int GRID_MAX_SPAN = 16;

    std::vector<const CollisionPiece*> g_near;

    struct WalkFrameStats
    {
        int blockedSteps = 0;
        bool unsettled = false;
        float low = 1e9f;
        float high = 0.0f;
        int jumps = 0;
        int lands = 0;
        float landSpeed = 0.0f;
    };

    struct WalkHud
    {
        float speed = 0.0f;
        float low = 0.0f;
        float high = 0.0f;
        float vy = 0.0f;
        bool grounded = false;
        float accel = 0.0f;
        size_t nearCount = 0;
        int steps = 0;
        int blocked = 0;
        bool unsettled = false;
        float dt = 0.0f;
        glm::vec3 feet{ 0.0f };
    };

    WalkFrameStats g_frame;
    WalkHud g_hud;
    std::ofstream g_log;
    std::string g_logName;
    uint32_t g_logRows = 0;
    double g_hudTime = 0.0;
    double g_logFlushTime = 0.0;

    void openWalkLog()
    {
        std::error_code error;
        std::filesystem::create_directories(BLACK_GLASS_LOG_DIR, error);
        std::time_t stamp = std::time(nullptr);
        char name[64];
        std::strftime(name, sizeof(name), "walk_%Y%m%d_%H%M%S.csv", std::localtime(&stamp));
        g_logName = name;
        g_log.open(std::string(BLACK_GLASS_LOG_DIR) + "/" + name);
        g_logRows = 0;
        g_logFlushTime = 0.0;
        g_frame = WalkFrameStats();
        if (g_log.is_open())
        {
            g_log << "time,rawDt,smoothDt,steps,alpha,speed,stepLow,stepHigh,vx,vy,vz,grounded,accel,near,blockedSteps,unsettled,jumps,lands,landSpeed,feetX,feetY,feetZ,eyeY,camY,bobY,landShown,roll,fovBoost\n";
        }
    }

    void closeWalkLog()
    {
        if (g_log.is_open())
        {
            g_log.close();
        }
    }
    std::unordered_map<int64_t, std::vector<uint32_t>> g_grid;
    std::vector<uint32_t> g_large;
    std::vector<uint32_t> g_stamp;
    uint32_t g_stampId = 0;

    int64_t cellKey(int cx, int cz)
    {
        return (static_cast<int64_t>(cx) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(cz));
    }

    int cellOf(float value)
    {
        return static_cast<int>(std::floor(value / GRID_CELL));
    }

    void buildGrid(const std::vector<CollisionPiece>& pieces)
    {
        g_grid.clear();
        g_large.clear();
        g_stamp.assign(pieces.size(), 0);
        g_stampId = 0;
        for (uint32_t i = 0; i < pieces.size(); i++)
        {
            const CollisionPiece& piece = pieces[i];
            int x0 = cellOf(piece.center.x - piece.reach);
            int x1 = cellOf(piece.center.x + piece.reach);
            int z0 = cellOf(piece.center.y - piece.reach);
            int z1 = cellOf(piece.center.y + piece.reach);
            if (x1 - x0 >= GRID_MAX_SPAN || z1 - z0 >= GRID_MAX_SPAN)
            {
                g_large.push_back(i);
                continue;
            }
            for (int cx = x0; cx <= x1; cx++)
            {
                for (int cz = z0; cz <= z1; cz++)
                {
                    g_grid[cellKey(cx, cz)].push_back(i);
                }
            }
        }
    }

    void gatherNear(const std::vector<CollisionPiece>& pieces, const glm::vec2& at)
    {
        g_near.clear();
        g_stampId++;
        auto consider = [&](uint32_t index)
        {
            if (g_stamp[index] == g_stampId)
            {
                return;
            }
            g_stamp[index] = g_stampId;
            const CollisionPiece& piece = pieces[index];
            glm::vec2 gap = at - piece.center;
            float limit = piece.reach + PLAYER_RADIUS + NEAR_MARGIN;
            if (glm::dot(gap, gap) <= limit * limit)
            {
                g_near.push_back(&piece);
            }
        };

        float span = PLAYER_RADIUS + NEAR_MARGIN;
        int x0 = cellOf(at.x - span);
        int x1 = cellOf(at.x + span);
        int z0 = cellOf(at.y - span);
        int z1 = cellOf(at.y + span);
        for (int cx = x0; cx <= x1; cx++)
        {
            for (int cz = z0; cz <= z1; cz++)
            {
                auto found = g_grid.find(cellKey(cx, cz));
                if (found == g_grid.end())
                {
                    continue;
                }
                for (uint32_t index : found->second)
                {
                    consider(index);
                }
            }
        }
        for (uint32_t index : g_large)
        {
            consider(index);
        }
    }

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
        piece.reach = glm::length(glm::max(glm::abs(minLocal), glm::abs(maxLocal)));
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
    buildGrid(m_collision);
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
    openWalkLog();
    setMouseCaptured(true);
}

void BlackGlass::exitWalkMode()
{
    if (!m_walkMode)
    {
        return;
    }
    m_walkMode = false;
    closeWalkLog();
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

    m_walkAccumulator += rawTime;

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
    m_walkBobPhase += horizontalSpeed * stride * rawTime;
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

    double now = glfwGetTime();
    const float accelInUse = m_walkGrounded ? GROUND_ACCEL : AIR_ACCEL;
    const float stepLow = g_frame.low > 1e8f ? horizontalSpeed : g_frame.low;

    if (g_log.is_open())
    {
        char row[512];
        std::snprintf(row, sizeof(row),
                      "%.4f,%.5f,%.5f,%d,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.0f,%zu,%d,%d,%d,%d,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.5f,%.5f,%.4f,%.4f\n",
                      now, rawTime, frameTime, steps, alpha, horizontalSpeed, stepLow, g_frame.high,
                      m_walkVelocity.x, m_walkVelocity.y, m_walkVelocity.z, m_walkGrounded ? 1 : 0, accelInUse,
                      g_near.size(), g_frame.blockedSteps, g_frame.unsettled ? 1 : 0, g_frame.jumps, g_frame.lands,
                      g_frame.landSpeed, feet.x, feet.y, feet.z, m_walkEyeY, m_camera.position.y,
                      m_walkBobY, m_walkLandShown, m_walkRoll, m_walkFovBoost);
        g_log << row;
        g_logRows++;
        if (now - g_logFlushTime >= 0.5)
        {
            g_log.flush();
            g_logFlushTime = now;
        }
    }

    if (now - g_hudTime >= 0.1)
    {
        g_hudTime = now;
        g_hud.speed = horizontalSpeed;
        g_hud.low = stepLow;
        g_hud.high = g_frame.high;
        g_hud.vy = m_walkVelocity.y;
        g_hud.grounded = m_walkGrounded;
        g_hud.accel = accelInUse;
        g_hud.nearCount = g_near.size();
        g_hud.steps = steps;
        g_hud.blocked = g_frame.blockedSteps;
        g_hud.unsettled = g_frame.unsettled;
        g_hud.dt = frameTime;
        g_hud.feet = feet;
    }
    g_frame = WalkFrameStats();
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

    const float stepUp = m_walkGrounded ? STEP_HEIGHT : AIR_STEP_HEIGHT;
    gatherNear(m_collision, startPosition);

    bool settled = false;
    for (int pass = 0; pass < RESOLVE_PASSES; pass++)
    {
        bool moved = false;
        for (const CollisionPiece* candidate : g_near)
        {
            const CollisionPiece& piece = *candidate;
            if (piece.baseY >= feet.y + PLAYER_HEIGHT - 0.01f)
            {
                continue;
            }

            glm::vec2 local = toLocal(piece, position);
            glm::vec2 nearest = glm::clamp(local, piece.minLocal, piece.maxLocal);
            float blockTop = piece.steep ? piece.baseY + piece.topOffset + piece.slope * (piece.maxLocal.y - piece.minLocal.y)
                                         : topAt(piece, nearest.y);
            if (blockTop <= feet.y + stepUp)
            {
                continue;
            }

            glm::vec2 offset = local - nearest;
            float distance = glm::length(offset);
            glm::vec2 push(0.0f);
            if (distance > 1e-5f)
            {
                if (distance >= PLAYER_RADIUS - PUSH_EPSILON)
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
            settled = true;
            break;
        }
    }
    glm::vec2 actual = position - startPosition;
    if (glm::dot(actual, actual) < glm::dot(intended, intended) - 1e-10f)
    {
        velocity = actual / dt;
    }
    if (!settled)
    {
        g_frame.unsettled = true;
    }
    if (glm::dot(actual, actual) < glm::dot(intended, intended) - 1e-10f)
    {
        g_frame.blockedSteps++;
    }
    {
        float stepSpeed = glm::length(velocity);
        g_frame.high = std::max(g_frame.high, stepSpeed);
        g_frame.low = std::min(g_frame.low, stepSpeed);
    }
    m_walkVelocity.x = velocity.x;
    m_walkVelocity.z = velocity.y;
    feet.x = position.x;
    feet.z = position.y;

    auto groundAt = [&](float referenceFeet) {
        float ground = FLOOR_HEIGHT;
        const float reach = PLAYER_RADIUS * 0.8f;
        const glm::vec2 samples[5] = { { 0.0f, 0.0f }, { reach, 0.0f }, { -reach, 0.0f }, { 0.0f, reach }, { 0.0f, -reach } };
        for (const CollisionPiece* candidate : g_near)
        {
            const CollisionPiece& piece = *candidate;
            for (const glm::vec2& sample : samples)
            {
                glm::vec2 local = toLocal(piece, position + sample);
                if (!insideRect(piece, local))
                {
                    continue;
                }
                float top = topAt(piece, local.y);
                if (top <= referenceFeet + stepUp)
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
            g_frame.jumps++;
        }
    }
    else
    {
        m_walkGrounded = false;
        m_walkVelocity.y -= GRAVITY * dt;
        float next = feet.y + m_walkVelocity.y * dt;

        if (m_walkVelocity.y > 0.0f)
        {
            for (const CollisionPiece* candidate : g_near)
            {
                const CollisionPiece& piece = *candidate;
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
            g_frame.lands++;
            g_frame.landSpeed = std::max(g_frame.landSpeed, -m_walkVelocity.y);
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

void BlackGlass::drawWalkDebug()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##walkdebug", nullptr, flags))
    {
        ImGui::Text("speed  %.2f  (step lo %.2f  hi %.2f)", g_hud.speed, g_hud.low, g_hud.high);
        ImGui::Text("vy     %.2f   %s   accel %.0f", g_hud.vy, g_hud.grounded ? "GROUND" : "AIR", g_hud.accel);
        ImGui::Text("near %zu   blocked %d   unsettled %d   steps %d", g_hud.nearCount, g_hud.blocked,
                    g_hud.unsettled ? 1 : 0, g_hud.steps);
        ImGui::Text("dt %.4f   feet %.2f %.2f %.2f", g_hud.dt, g_hud.feet.x, g_hud.feet.y, g_hud.feet.z);
        ImGui::Text("log/%s  (%u rows)", g_logName.c_str(), g_logRows);
    }
    ImGui::End();
}
