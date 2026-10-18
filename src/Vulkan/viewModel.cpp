#include "../BlackGlass.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    constexpr const char* VIEW_MODEL_FILE = "fps_arms_saiga_animations_remake.glb";
    constexpr float VIEW_MODEL_YAW_DEGREES = 180.0f;
    constexpr float WALK_BOB_CYCLES_PER_CLIP = 2.0f;
    constexpr float RUN_BOB_CYCLES_PER_CLIP = 1.0f;
    constexpr float TWO_PI = 6.2831853f;
    constexpr float MIN_BLEND_WEIGHT = 1e-4f;
    const glm::vec3 VIEW_MODEL_FALLBACK_EYE(0.0f, 0.56f, 0.0f);
    constexpr float TUNE_MOVE_SPEED = 0.15f;
    constexpr float TUNE_SIZE_RATE = 0.5f;

    struct Pose
    {
        std::vector<glm::vec3> translation;
        std::vector<glm::quat> rotation;
        std::vector<glm::vec3> scale;
    };

    Pose g_poseBlend;
    Pose g_poseScratch;
    std::vector<glm::mat4> g_world;

    void restPose(const Skeleton& skeleton, Pose& pose)
    {
        size_t count = skeleton.nodes.size();
        pose.translation.resize(count);
        pose.rotation.resize(count);
        pose.scale.resize(count);
        for (size_t i = 0; i < count; i++)
        {
            pose.translation[i] = skeleton.nodes[i].translation;
            pose.rotation[i] = skeleton.nodes[i].rotation;
            pose.scale[i] = skeleton.nodes[i].scale;
        }
    }

    void samplePose(const Skeleton& skeleton, const AnimationClip& clip, float time, Pose& pose)
    {
        restPose(skeleton, pose);
        if (clip.duration > 1e-6f)
        {
            time = std::fmod(time, clip.duration);
            if (time < 0.0f)
            {
                time += clip.duration;
            }
        }

        for (const AnimChannel& channel : clip.channels)
        {
            const std::vector<float>& times = channel.times;
            size_t upper = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), time) - times.begin());

            glm::vec4 value;
            if (upper == 0)
            {
                value = channel.values.front();
            }
            else if (upper >= times.size())
            {
                value = channel.values.back();
            }
            else
            {
                const glm::vec4& a = channel.values[upper - 1];
                const glm::vec4& b = channel.values[upper];
                float span = times[upper] - times[upper - 1];
                float fraction = span > 1e-8f ? (time - times[upper - 1]) / span : 0.0f;
                if (channel.step)
                {
                    value = a;
                }
                else if (channel.path == 1)
                {
                    glm::quat qa(a.w, a.x, a.y, a.z);
                    glm::quat qb(b.w, b.x, b.y, b.z);
                    glm::quat q = glm::normalize(glm::slerp(qa, qb, fraction));
                    value = glm::vec4(q.x, q.y, q.z, q.w);
                }
                else
                {
                    value = glm::mix(a, b, fraction);
                }
            }

            size_t node = static_cast<size_t>(channel.node);
            if (channel.path == 0)
            {
                pose.translation[node] = glm::vec3(value);
            }
            else if (channel.path == 1)
            {
                pose.rotation[node] = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
            }
            else
            {
                pose.scale[node] = glm::vec3(value);
            }
        }
    }

    void blendPose(Pose& target, const Pose& other, float fraction)
    {
        for (size_t i = 0; i < target.translation.size(); i++)
        {
            target.translation[i] = glm::mix(target.translation[i], other.translation[i], fraction);
            target.rotation[i] = glm::slerp(target.rotation[i], other.rotation[i], fraction);
            target.scale[i] = glm::mix(target.scale[i], other.scale[i], fraction);
        }
    }

    int findClip(const Skeleton& skeleton, const char* needle)
    {
        for (size_t i = 0; i < skeleton.clips.size(); i++)
        {
            if (skeleton.clips[i].name.find(needle) != std::string::npos)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
}

void BlackGlass::loadViewModel()
{
    if (m_viewModelTried)
    {
        return;
    }
    m_viewModelTried = true;

    std::string path = std::string(BLACK_GLASS_ASSET_DIR) + "/propri/" + VIEW_MODEL_FILE;
    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        std::cout << "  [loadViewModel] No view model found at " << path << "\n";
        return;
    }

    std::cout << "  [loadViewModel] Loading " << path << " ... \n";
    auto asset = std::make_shared<ModelAsset>();
    try
    {
        MeshData mesh = loadGltf(path);
        if (mesh.skinVertices.empty() || mesh.skeleton.joints.empty())
        {
            throw std::runtime_error("model has no skin");
        }
        classifyPrimitives(mesh);

        asset->name = "View model";
        asset->path = path;

        vkDeviceWaitIdle(M_DEVICE);
        uploadModelGeometry(*asset, mesh);
        createModelTextures(*asset, mesh);

        const Skeleton& skeleton = *asset->skeleton;
        m_viewIdleClip = findClip(skeleton, "Idle");
        m_viewWalkClip = findClip(skeleton, "Walk");
        m_viewRunClip = findClip(skeleton, "Run");

        m_viewEye = VIEW_MODEL_FALLBACK_EYE;
        for (size_t j = 0; j < skeleton.jointNames.size(); j++)
        {
            if (skeleton.jointNames[j].find("Cam") != std::string::npos)
            {
                restPose(skeleton, g_poseBlend);
                g_world.resize(skeleton.nodes.size());
                for (int node : skeleton.order)
                {
                    const SkeletonNode& source = skeleton.nodes[node];
                    glm::mat4 local = source.hasMatrix
                        ? source.matrix
                        : glm::translate(glm::mat4(1.0f), g_poseBlend.translation[node])
                            * glm::mat4_cast(g_poseBlend.rotation[node])
                            * glm::scale(glm::mat4(1.0f), g_poseBlend.scale[node]);
                    g_world[node] = source.parent < 0 ? local : g_world[source.parent] * local;
                }
                m_viewEye = glm::vec3(g_world[skeleton.joints[j]][3]);
                m_viewUnits = std::max(glm::length(glm::vec3(g_world[skeleton.joints[j]][0])), 1e-4f);
                break;
            }
        }

        m_viewJoints.assign(std::min<size_t>(skeleton.joints.size(), MAX_JOINTS), glm::mat4(1.0f));
        if (skeleton.joints.size() > MAX_JOINTS)
        {
            std::cout << "  [loadViewModel] WARN: " << skeleton.joints.size() << " joints, only "
                      << MAX_JOINTS << " are used.\n";
        }

        m_viewModel = std::make_unique<Model>();
        m_viewModel->asset = asset;
        m_viewModel->name = asset->name;
        m_viewModel->viewModel = true;
        m_viewIdleTime = 0.0f;
        m_viewModelReady = true;

        std::cout << "  [loadViewModel] SUCCESS - idle " << m_viewIdleClip << ", walk " << m_viewWalkClip
                  << ", run " << m_viewRunClip << ", units " << m_viewUnits << ", eye (" << m_viewEye.x << ", " << m_viewEye.y << ", "
                  << m_viewEye.z << ").\n";
    }
    catch (const std::exception& failure)
    {
        std::cout << "  [loadViewModel] WARN: " << failure.what() << "\n";
        destroyAsset(*asset);
        m_viewModel.reset();
        m_viewModelReady = false;
    }
}

void BlackGlass::updateViewModel(float deltaTime, float sprintFraction)
{
    if (!m_viewModelReady || !m_viewModel || !m_viewModel->asset->skeleton)
    {
        return;
    }

    const Skeleton& skeleton = *m_viewModel->asset->skeleton;

    float moveWeight = std::clamp(m_walkBobAmount, 0.0f, 1.0f);
    float runWeight = moveWeight * std::clamp(sprintFraction, 0.0f, 1.0f);
    float walkWeight = moveWeight - runWeight;
    float idleWeight = 1.0f - moveWeight;

    m_viewIdleTime += deltaTime;

    auto cycleTime = [&](int clip, float cycles)
    {
        float cyclePosition = m_walkBobPhase / (TWO_PI * cycles);
        cyclePosition -= std::floor(cyclePosition);
        return cyclePosition * skeleton.clips[clip].duration;
    };

    float accumulated = 0.0f;
    bool first = true;
    auto addClip = [&](int clip, float weight, float time)
    {
        if (clip < 0 || weight < MIN_BLEND_WEIGHT)
        {
            return;
        }
        Pose& target = first ? g_poseBlend : g_poseScratch;
        samplePose(skeleton, skeleton.clips[clip], time, target);
        if (!first)
        {
            blendPose(g_poseBlend, g_poseScratch, weight / (accumulated + weight));
        }
        accumulated += weight;
        first = false;
    };

    addClip(m_viewIdleClip, idleWeight, m_viewIdleTime);
    if (m_viewWalkClip >= 0)
    {
        addClip(m_viewWalkClip, walkWeight, cycleTime(m_viewWalkClip, WALK_BOB_CYCLES_PER_CLIP));
    }
    if (m_viewRunClip >= 0)
    {
        addClip(m_viewRunClip, runWeight, cycleTime(m_viewRunClip, RUN_BOB_CYCLES_PER_CLIP));
    }
    if (first)
    {
        restPose(skeleton, g_poseBlend);
    }

    g_world.resize(skeleton.nodes.size());
    for (int node : skeleton.order)
    {
        const SkeletonNode& source = skeleton.nodes[node];
        glm::mat4 local = source.hasMatrix
            ? source.matrix
            : glm::translate(glm::mat4(1.0f), g_poseBlend.translation[node])
                * glm::mat4_cast(g_poseBlend.rotation[node])
                * glm::scale(glm::mat4(1.0f), g_poseBlend.scale[node]);
        g_world[node] = source.parent < 0 ? local : g_world[source.parent] * local;
    }

    for (size_t j = 0; j < m_viewJoints.size(); j++)
    {
        m_viewJoints[j] = g_world[skeleton.joints[j]] * skeleton.inverseBind[j];
    }

    auto held = [&](int key) { return glfwGetKey(m_window, key) == GLFW_PRESS; };
    float tuneStep = deltaTime * TUNE_MOVE_SPEED;
    if (held(GLFW_KEY_LEFT)) m_viewOffset.x -= tuneStep;
    if (held(GLFW_KEY_RIGHT)) m_viewOffset.x += tuneStep;
    if (held(GLFW_KEY_UP)) m_viewOffset.y += tuneStep;
    if (held(GLFW_KEY_DOWN)) m_viewOffset.y -= tuneStep;
    if (held(GLFW_KEY_PERIOD)) m_viewOffset.z -= tuneStep;
    if (held(GLFW_KEY_COMMA)) m_viewOffset.z += tuneStep;
    if (held(GLFW_KEY_RIGHT_BRACKET)) m_viewSize *= 1.0f + deltaTime * TUNE_SIZE_RATE;
    if (held(GLFW_KEY_LEFT_BRACKET)) m_viewSize *= 1.0f - deltaTime * TUNE_SIZE_RATE;
    m_viewSize = std::clamp(m_viewSize, 0.1f, 3.0f);

    glm::mat4 cameraWorld = glm::inverse(m_camera.view());
    m_viewModel->transform = cameraWorld
        * glm::translate(glm::mat4(1.0f), m_viewOffset)
        * glm::rotate(glm::mat4(1.0f), glm::radians(VIEW_MODEL_YAW_DEGREES), glm::vec3(0.0f, 1.0f, 0.0f))
        * glm::scale(glm::mat4(1.0f), glm::vec3(m_viewSize / m_viewUnits))
        * glm::translate(glm::mat4(1.0f), -m_viewEye);

    char probe[256];
    std::string probeText;
    for (size_t j = 0; j < skeleton.jointNames.size() && j < m_viewJoints.size(); j++)
    {
        const std::string& jointName = skeleton.jointNames[j];
        if (jointName.find("Hand_R") == std::string::npos && jointName.find("Hand_L") == std::string::npos &&
            jointName.find("Saiga") == std::string::npos)
        {
            continue;
        }
        glm::vec3 local = glm::vec3(m_viewJoints[j] * glm::inverse(skeleton.inverseBind[j])[3]);
        glm::vec4 clip = m_viewProj * m_viewModel->transform * glm::vec4(local, 1.0f);
        glm::vec3 viewSpace = glm::vec3(m_camera.view() * m_viewModel->transform * glm::vec4(local, 1.0f));
        std::snprintf(probe, sizeof(probe), "%s cam(%.2f %.2f %.2f) ndc(%.2f %.2f) w %.2f\n",
                      jointName.substr(0, 8).c_str(), viewSpace.x, viewSpace.y, viewSpace.z,
                      clip.w != 0.0f ? clip.x / clip.w : 0.0f, clip.w != 0.0f ? clip.y / clip.w : 0.0f, clip.w);
        probeText += probe;
    }
    const AllocatedBuffer& skinBuffer = m_viewModel->asset->skinBuffer;
    std::snprintf(probe, sizeof(probe), "arms: prims %zu skinBuf %d pipeline %d joints %zu\n",
                  m_viewModel->asset->primitives.size(), skinBuffer.buffer != VK_NULL_HANDLE ? 1 : 0,
                  M_VIEWMODELPIPELINE != VK_NULL_HANDLE ? 1 : 0, m_viewJoints.size());
    std::snprintf(probe, sizeof(probe), "arms offset %.3f %.3f %.3f  size %.3f\narrows: left/right/up/down   , . : depth   [ ] : size\n",
                  m_viewOffset.x, m_viewOffset.y, m_viewOffset.z, m_viewSize);
    m_viewDebug = probe + probeText;
}

void BlackGlass::destroyViewModel()
{
    if (m_viewModel && m_viewModel->asset)
    {
        destroyAsset(*m_viewModel->asset);
    }
    m_viewModel.reset();
    m_viewModelReady = false;
}
