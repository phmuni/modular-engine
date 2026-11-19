// Sampling glTF animation clips and crossfading between them.

#include "systems/animationSystem.h"

#include "components/animator.h"
#include "components/model.h"
#include "rendering/resources/skeletonPose.h"
#include "systems/resourceSystem.h"
#include <algorithm>
#include <cmath>

namespace {
// Where t falls between two keys: the earlier key and how far toward the next (0 to 1)
void locate(const std::vector<float> &times, float t, size_t &key, float &blend, float &interval) {
  if (t <= times.front() || times.size() == 1) {
    key = 0;
    blend = 0.0f;
    interval = 0.0f;
    return;
  }
  if (t >= times.back()) {
    key = times.size() - 1;
    blend = 0.0f;
    interval = 0.0f;
    return;
  }
  key = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), t) - times.begin()) - 1;
  interval = times[key + 1] - times[key];
  blend = interval > 0.0f ? (t - times[key]) / interval : 0.0f;
}

glm::vec4 hermite(const glm::vec4 &p0, const glm::vec4 &m0, const glm::vec4 &p1, const glm::vec4 &m1, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * m1;
}

glm::quat toQuat(const glm::vec4 &v) { return glm::normalize(glm::quat(v.w, v.x, v.y, v.z)); }

void blendPoses(std::vector<NodePose> &into, const std::vector<NodePose> &from, float weightOfInto) {
  for (size_t i = 0; i < into.size() && i < from.size(); ++i) {
    into[i].translation = glm::mix(from[i].translation, into[i].translation, weightOfInto);
    into[i].rotation = glm::slerp(from[i].rotation, into[i].rotation, weightOfInto);
    into[i].scale = glm::mix(from[i].scale, into[i].scale, weightOfInto);
  }
}

// Moves `time` on by `delta` within the clip: around when looping, stopping at an end otherwise
float advance(float time, float delta, float duration, bool loop, bool &finished) {
  finished = false;
  time += delta;
  if (duration <= 0.0f)
    return 0.0f;
  if (loop) {
    time = std::fmod(time, duration);
    return time < 0.0f ? time + duration : time;
  }
  if (time >= duration || time <= 0.0f) {
    finished = true;
    return std::clamp(time, 0.0f, duration);
  }
  return time;
}

const AnimationClip *findClip(const MeshAnimation &animation, const std::string &name) {
  if (animation.clips.empty())
    return nullptr;
  if (name.empty())
    return &animation.clips.front();
  const int index = animation.findClip(name);
  return index >= 0 ? &animation.clips[index] : nullptr;
}
} // namespace

void AnimationSystem::sample(const MeshAnimation &animation, const AnimationClip &clip, float time,
                             std::vector<NodePose> &pose) {
  pose = SkeletonPose::rest(animation.skeleton);
  for (const AnimationChannel &channel : clip.channels) {
    if (channel.node < 0 || channel.node >= static_cast<int>(pose.size()))
      continue;
    size_t key = 0;
    float blend = 0.0f, interval = 0.0f;
    locate(channel.times, time, key, blend, interval);
    const size_t next = std::min(key + 1, channel.times.size() - 1);

    glm::vec4 value;
    glm::quat rotation;
    const bool isRotation = channel.path == AnimationChannel::Path::Rotation;
    switch (channel.interpolation) {
    case AnimationChannel::Interpolation::Step:
      value = channel.values[key];
      rotation = toQuat(value);
      break;
    case AnimationChannel::Interpolation::Linear:
      value = glm::mix(channel.values[key], channel.values[next], blend);
      rotation = glm::slerp(toQuat(channel.values[key]), toQuat(channel.values[next]), blend);
      break;
    case AnimationChannel::Interpolation::CubicSpline: {
      // Three values per key: in-tangent, value, out-tangent; tangents scale with the time between keys
      const glm::vec4 &p0 = channel.values[key * 3 + 1], &m0 = channel.values[key * 3 + 2];
      const glm::vec4 &p1 = channel.values[next * 3 + 1], &m1 = channel.values[next * 3];
      value = key == next ? p0 : hermite(p0, m0 * interval, p1, m1 * interval, blend);
      rotation = toQuat(value);
      break;
    }
    }

    NodePose &node = pose[channel.node];
    if (isRotation)
      node.rotation = rotation;
    else if (channel.path == AnimationChannel::Path::Translation)
      node.translation = glm::vec3(value);
    else
      node.scale = glm::vec3(value);
  }
}

void AnimationSystem::update(float deltaTime) {
  // Joints of entities that are gone, or no longer animated, are dropped below
  for (auto &[entity, joints] : m_joints)
    joints.clear();

  std::vector<NodePose> pose, fadingPose;
  m_componentManager.forEachComponent<Animator>([&](Entity entity, Animator &animator) {
    const Model *model = m_componentManager.tryGet<Model>(entity);
    const MeshAnimation *animation = model ? m_resourceSystem.getMesh(model->meshHandle).getAnimation() : nullptr;
    if (!animation)
      return;
    const AnimationClip *clip = findClip(*animation, animator.clip);
    if (!clip) {
      m_joints[entity] = animation->restJointMatrices;
      return;
    }

    bool finished = false;
    if (animator.playing) {
      animator.time = advance(animator.time, deltaTime * animator.speed, clip->duration, animator.loop, finished);
      if (finished)
        animator.playing = false;
    }
    sample(*animation, *clip, animator.time, pose);

    // Fading out the clip it switched from: both play on, mixed by how far the fade is
    if (!animator.fadeFrom.empty() && animator.fadeDuration > 0.0f) {
      if (const AnimationClip *from = findClip(*animation, animator.fadeFrom)) {
        bool fromFinished = false;
        animator.fadeFromTime = advance(animator.fadeFromTime, deltaTime * animator.speed, from->duration,
                                        animator.loop, fromFinished);
        animator.fadeElapsed += deltaTime;
        sample(*animation, *from, animator.fadeFromTime, fadingPose);
        blendPoses(pose, fadingPose, std::clamp(animator.fadeElapsed / animator.fadeDuration, 0.0f, 1.0f));
      }
      if (animator.fadeElapsed >= animator.fadeDuration)
        animator.fadeFrom.clear();
    }

    SkeletonPose::jointMatrices(animation->skeleton, pose, m_globals, m_joints[entity]);
  });

  for (auto it = m_joints.begin(); it != m_joints.end();)
    it = it->second.empty() ? m_joints.erase(it) : std::next(it);
}

const std::vector<glm::mat4> *AnimationSystem::getJointMatrices(Entity entity) const {
  auto it = m_joints.find(entity);
  if (it != m_joints.end())
    return &it->second;
  const Model *model = m_componentManager.tryGet<Model>(entity);
  const MeshAnimation *animation = model ? m_resourceSystem.getMesh(model->meshHandle).getAnimation() : nullptr;
  return animation ? &animation->restJointMatrices : nullptr;
}
