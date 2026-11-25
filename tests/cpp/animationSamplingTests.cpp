// Skeletal pose evaluation: AnimationSystem::sample and SkeletonPose::jointMatrices against a plain implementation of
// the algorithm they replaced (per-channel key search, normalize and slerp at every sample, full 4x4 products), for
// every clip of the knight and the tentacle and for a hand-made animation with every kind of channel; and that a
// sample of some nodes only equals the full sample on those nodes.

#include "components/model.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "rendering/resources/skeletonPose.h"
#include "systems/animationSystem.h"
#include "systems/resourceSystem.h"
#include "testing.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float kTolerance = 1e-5f;

// A channel as it is read from a file, before the clip prepares it
struct RawChannel {
  int node;
  AnimationChannel::Path path;
  AnimationChannel::Interpolation interpolation;
  std::vector<float> times;
  std::vector<glm::vec4> values;
};

glm::quat referenceQuat(const glm::vec4 &v) { return glm::normalize(glm::quat(v.w, v.x, v.y, v.z)); }

void referenceLocate(const std::vector<float> &times, float t, size_t &key, float &blend, float &interval) {
  if (t <= times.front() || times.size() == 1) {
    key = 0;
    blend = interval = 0.0f;
    return;
  }
  if (t >= times.back()) {
    key = times.size() - 1;
    blend = interval = 0.0f;
    return;
  }
  key = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), t) - times.begin()) - 1;
  interval = times[key + 1] - times[key];
  blend = interval > 0.0f ? (t - times[key]) / interval : 0.0f;
}

glm::vec4 referenceHermite(const glm::vec4 &p0, const glm::vec4 &m0, const glm::vec4 &p1, const glm::vec4 &m1,
                           float t) {
  const float t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * m1;
}

std::vector<NodePose> referenceSample(const Skeleton &skeleton, const std::vector<RawChannel> &channels, float time) {
  std::vector<NodePose> pose(skeleton.nodes.size());
  for (size_t i = 0; i < pose.size(); ++i)
    pose[i] = {skeleton.nodes[i].translation, skeleton.nodes[i].rotation, skeleton.nodes[i].scale};
  for (const RawChannel &channel : channels) {
    size_t key = 0;
    float blend = 0.0f, interval = 0.0f;
    referenceLocate(channel.times, time, key, blend, interval);
    const size_t next = std::min(key + 1, channel.times.size() - 1);
    glm::vec4 value;
    glm::quat rotation;
    switch (channel.interpolation) {
    case AnimationChannel::Interpolation::Step:
      value = channel.values[key];
      rotation = referenceQuat(value);
      break;
    case AnimationChannel::Interpolation::Linear:
      value = glm::mix(channel.values[key], channel.values[next], blend);
      rotation = glm::slerp(referenceQuat(channel.values[key]), referenceQuat(channel.values[next]), blend);
      break;
    case AnimationChannel::Interpolation::CubicSpline: {
      const glm::vec4 &p0 = channel.values[key * 3 + 1], &m0 = channel.values[key * 3 + 2];
      const glm::vec4 &p1 = channel.values[next * 3 + 1], &m1 = channel.values[next * 3];
      value = key == next ? p0 : referenceHermite(p0, m0 * interval, p1, m1 * interval, blend);
      rotation = referenceQuat(value);
      break;
    }
    }
    NodePose &node = pose[channel.node];
    if (channel.path == AnimationChannel::Path::Rotation)
      node.rotation = rotation;
    else if (channel.path == AnimationChannel::Path::Translation)
      node.translation = glm::vec3(value);
    else
      node.scale = glm::vec3(value);
  }
  return pose;
}

std::vector<glm::mat4> referenceJoints(const Skeleton &skeleton, const std::vector<NodePose> &pose) {
  std::vector<glm::mat4> globals(skeleton.nodes.size());
  for (int node : skeleton.order) {
    const NodePose &local = pose[node];
    glm::mat4 matrix = glm::translate(glm::mat4(1.0f), local.translation) * glm::mat4_cast(local.rotation);
    matrix = glm::scale(matrix, local.scale);
    const int parent = skeleton.nodes[node].parent;
    globals[node] = parent >= 0 ? globals[parent] * matrix : matrix;
  }
  std::vector<glm::mat4> joints(skeleton.jointNodes.size());
  for (size_t j = 0; j < joints.size(); ++j)
    joints[j] = globals[skeleton.jointNodes[j]] * skeleton.inverseBind[j];
  return joints;
}

float matrixDifference(const glm::mat4 &a, const glm::mat4 &b) {
  float most = 0.0f;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      most = std::max(most, std::fabs(a[c][r] - b[c][r]));
  return most;
}

std::vector<RawChannel> rawChannelsOf(const AnimationClip &clip) {
  std::vector<RawChannel> raw;
  for (const AnimationChannel &channel : clip.channels)
    raw.push_back({channel.node, channel.path, channel.interpolation, clip.tracks[channel.track].times, channel.values});
  // A constant channel is a channel of one key
  for (const ConstantChannel &constant : clip.constants)
    raw.push_back({constant.node, constant.path, AnimationChannel::Interpolation::Linear, {0.0f}, {constant.value}});
  return raw;
}

// The times to try: before the first key, each key and a little either side, between, after the last, and a spread
std::vector<float> timesFor(const AnimationClip &clip) {
  std::vector<float> times = {-1.0f, -0.001f, 0.0f, clip.duration, clip.duration + 0.5f};
  for (const AnimationTrack &track : clip.tracks)
    for (size_t k = 0; k < track.times.size(); ++k) {
      times.push_back(track.times[k]);
      times.push_back(track.times[k] + 1e-4f);
      times.push_back(track.times[k] - 1e-4f);
      if (k + 1 < track.times.size())
        times.push_back(0.5f * (track.times[k] + track.times[k + 1]));
    }
  for (int i = 0; i <= 200; ++i)
    times.push_back(clip.duration * static_cast<float>(i) / 200.0f * 1.003f);
  return times;
}

// The worst difference of any joint matrix between the new code and the reference, over `times`
float worstDifference(const MeshAnimation &animation, const AnimationClip &clip, const std::vector<RawChannel> &raw) {
  std::vector<NodePose> pose;
  std::vector<glm::mat4> globals, joints;
  float worst = 0.0f;
  for (float time : timesFor(clip)) {
    AnimationSystem::sample(animation, clip, time, pose);
    SkeletonPose::jointMatrices(animation.skeleton, pose, globals, joints);
    const std::vector<glm::mat4> expected =
        referenceJoints(animation.skeleton, referenceSample(animation.skeleton, raw, time));
    for (size_t j = 0; j < joints.size(); ++j)
      worst = std::max(worst, matrixDifference(joints[j], expected[j]));
  }
  return worst;
}

// A small animation with every kind of channel, in a chain of four nodes with an odd rest pose
struct HandMade {
  MeshAnimation animation;
  std::vector<RawChannel> raw;
};

HandMade makeHandMade() {
  HandMade made;
  Skeleton &skeleton = made.animation.skeleton;
  skeleton.nodes.resize(5);
  const int parents[] = {-1, 0, 1, 2, 1};
  for (int i = 0; i < 5; ++i) {
    skeleton.nodes[i].parent = parents[i];
    skeleton.nodes[i].translation = glm::vec3(0.3f * i, 1.0f - 0.2f * i, 0.1f);
    skeleton.nodes[i].rotation = glm::normalize(glm::quat(0.9f - 0.1f * i, 0.1f * i, 0.2f, 0.3f));
    skeleton.nodes[i].scale = glm::vec3(1.0f);
    skeleton.order.push_back(i);
    skeleton.jointNodes.push_back(i);
    skeleton.inverseBind.push_back(glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(0.5f * i, 1.0f, -0.25f))));
  }
  skeleton.nodes[0].scale = glm::vec3(1.0f, 2.0f, 0.5f);   // non-uniform, at the root
  skeleton.nodes[2].scale = glm::vec3(-1.0f, 1.5f, 1.0f);  // negative
  // A joint whose inverse bind matrix is not affine takes the full product
  skeleton.inverseBind[4][1][3] = 0.2f;

  AnimationClip clip;
  clip.name = "everything";
  using Path = AnimationChannel::Path;
  using Interp = AnimationChannel::Interpolation;
  auto add = [&](int node, Path path, Interp interpolation, std::vector<float> times, std::vector<glm::vec4> values) {
    AnimationChannel channel;
    channel.node = node;
    channel.path = path;
    channel.interpolation = interpolation;
    channel.values = values;
    made.raw.push_back({node, path, interpolation, times, values});
    clip.addChannel(std::move(channel), std::move(times));
  };
  // Rotations, linear: unnormalized keys, a neighbour on the far side (key 2 is minus key 1, as a rotation), a pair so
  // close that they are lerped, and a half turn between keys 3 and 4
  add(1, Path::Rotation, Interp::Linear, {0.0f, 1.0f, 2.0f, 3.0f, 4.0f},
      {{0.0f, 0.0f, 0.0f, 2.0f}, {0.0f, 0.6f, 0.0f, 0.8f}, {0.0f, -1.2f, 0.0f, -1.6f},
       {0.0f, -1.2f, 0.00001f, -1.6f}, {0.0f, 1.0f, 0.0f, 0.0f}});
  add(2, Path::Scale, Interp::Linear, {0.0f, 1.0f, 2.0f, 3.0f, 4.0f},
      {{1.0f, 1.0f, 1.0f, 0.0f}, {-1.0f, 2.0f, 0.5f, 0.0f}, {-2.0f, -1.0f, 1.0f, 0.0f}, {1.5f, 1.5f, -1.5f, 0.0f},
       {1.0f, 1.0f, 1.0f, 0.0f}});
  add(3, Path::Translation, Interp::Linear, {0.0f, 1.0f, 2.0f, 3.0f, 4.0f},
      {{0, 0, 0, 0}, {1, 2, 3, 0}, {-1, 0, 2, 0}, {0, 0, 0, 0}, {5, 5, 5, 0}});
  // Step, with its own times
  add(1, Path::Translation, Interp::Step, {0.0f, 0.5f, 2.5f}, {{0, 0, 0, 0}, {0.4f, 0.1f, 0.0f, 0}, {-0.4f, 0.2f, 0.1f, 0}});
  add(4, Path::Rotation, Interp::Step, {0.0f, 1.5f, 3.0f},
      {{0.0f, 0.0f, 0.0f, 3.0f}, {0.5f, 0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, -2.0f, -2.0f}});
  add(4, Path::Scale, Interp::Step, {0.0f, 1.5f, 3.0f}, {{1, 1, 1, 0}, {2, 2, 2, 0}, {0.5f, 3.0f, 1.0f, 0}});
  // Cubic spline: in-tangent, value, out-tangent per key, with its own times
  add(2, Path::Rotation, Interp::CubicSpline, {0.0f, 1.5f, 3.0f, 4.0f},
      {{0, 0, 0, 0},       {0.0f, 0.0f, 0.0f, 1.0f}, {0.1f, 0.2f, 0.0f, 0.0f},   {0.0f, 0.1f, 0.0f, 0.0f},
       {0.0f, 0.6f, 0.0f, 0.8f}, {0.0f, 0.2f, 0.1f, 0.0f}, {0.0f, 0.1f, 0.0f, 0.0f}, {0.0f, -0.6f, 0.0f, -0.8f},
       {0.0f, 0.0f, 0.3f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}});
  add(3, Path::Scale, Interp::CubicSpline, {0.0f, 2.0f, 4.0f},
      {{0, 0, 0, 0}, {1, 1, 1, 0}, {0.2f, -0.1f, 0.0f, 0}, {0.1f, 0.1f, 0.1f, 0}, {2, -1, 1, 0}, {0.0f, 0.3f, 0.0f, 0},
       {0.0f, 0.0f, 0.1f, 0}, {1, 1, 2, 0}, {0, 0, 0, 0}});
  // Keys that share a time (a step in a linear channel), and channels of one key (one target takes only one channel)
  add(4, Path::Translation, Interp::Linear, {0.0f, 1.0f, 1.0f, 2.0f}, {{0, 0, 0, 0}, {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}});
  add(3, Path::Rotation, Interp::Linear, {1.0f}, {{0.0f, 0.0f, 3.0f, 4.0f}});
  add(0, Path::Scale, Interp::Linear, {0.5f}, {{1.0f, 2.0f, 3.0f, 0.0f}});
  // Channels that never change (they are laid over the rest pose once), and one that changes only in its last key
  add(0, Path::Translation, Interp::Linear, {0.0f, 1.0f, 2.0f}, {{0.2f, 0.1f, 0, 0}, {0.2f, 0.1f, 0, 0}, {0.2f, 0.1f, 0, 0}});
  add(1, Path::Scale, Interp::Step, {0.0f, 2.0f}, {{1.5f, 1.0f, 0.5f, 0}, {1.5f, 1.0f, 0.5f, 0}});
  add(0, Path::Rotation, Interp::Linear, {0.0f, 1.0f}, {{0.0f, 0.0f, 3.0f, 4.0f}, {0.0f, 0.0f, 3.0f, 4.0f}});
  add(2, Path::Translation, Interp::Linear, {0.0f, 1.0f, 2.0f}, {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1.0001f, 0, 0}});
  made.animation.clips.push_back(std::move(clip));
  return made;
}

} // namespace

TEST_CASE(sampling_the_hand_made_clip_matches_the_reference) {
  HandMade made = makeHandMade();
  const AnimationClip &clip = made.animation.clips[0];
  // Channels with equal times share a track, and those that never change are not channels at all
  CHECK_EQ(clip.channels.size(), size_t(10));
  CHECK_EQ(clip.constants.size(), size_t(5));
  CHECK_EQ(clip.tracks.size(), size_t(7));
  size_t counted = 0;
  for (const AnimationTrack &track : clip.tracks) {
    counted += track.channelCount;
    for (size_t c = track.firstChannel; c < track.firstChannel + track.channelCount; ++c)
      CHECK(clip.tracks[clip.channels[c].track].times == track.times);
  }
  CHECK_EQ(counted, clip.channels.size());
  CHECK_NEAR(clip.duration, 4.0f, 0.0f);

  // Without a rest pose stored in the animation, and with it
  const float without = worstDifference(made.animation, clip, made.raw);
  made.animation.restPose = SkeletonPose::rest(made.animation.skeleton);
  const float with = worstDifference(made.animation, clip, made.raw);
  CHECK_NEAR(without, 0.0f, kTolerance);
  CHECK_NEAR(with, 0.0f, kTolerance);
}

TEST_CASE(sampling_the_models_clips_matches_the_reference) {
  const std::string models[] = {"models/knight/knight.glb", "models/tentacle/tentacle.glb"};
  std::vector<Entity> entities;
  for (const std::string &model : models)
    entities.push_back(engine.create(model, Desc(model)).id());
  steps.step(5, [&engine, entities]() {
    for (Entity entity : entities) {
      const MeshAnimation *animation = engine.getSystemManager()
                                           .getSystem<ResourceSystem>()
                                           .getMesh(engine.get<Model>(entity).meshHandle)
                                           .getAnimation();
      CHECK(animation != nullptr);
      if (!animation)
        continue;
      CHECK(!animation->clips.empty());
      CHECK_EQ(animation->restPose.size(), animation->skeleton.nodes.size());
      for (const AnimationClip &clip : animation->clips) {
        const float worst = worstDifference(*animation, clip, rawChannelsOf(clip));
        CHECK_NEAR(worst, 0.0f, kTolerance);
        // Few tracks for many channels is the point of sharing them
        CHECK(clip.tracks.size() * 2 <= clip.channels.size() || clip.channels.size() < 8);
      }
    }
  });
}

TEST_CASE(sampling_some_nodes_equals_sampling_all_on_those_nodes) {
  const std::string models[] = {"models/knight/knight.glb", "models/tentacle/tentacle.glb"};
  std::vector<Entity> entities;
  for (const std::string &model : models)
    entities.push_back(engine.create(model, Desc(model)).id());
  steps.step(5, [&engine, entities]() {
    for (Entity entity : entities) {
      const MeshAnimation *animation = engine.getSystemManager()
                                           .getSystem<ResourceSystem>()
                                           .getMesh(engine.get<Model>(entity).meshHandle)
                                           .getAnimation();
      CHECK(animation != nullptr);
      if (!animation)
        continue;
      const size_t count = animation->skeleton.nodes.size();
      for (int pattern = 0; pattern < 3; ++pattern) {
        std::vector<uint8_t> active(count);
        for (size_t i = 0; i < count; ++i)
          active[i] = pattern == 0 ? (i % 3 != 0) : pattern == 1 ? (i % 2 == 0) : (i < count / 2);
        for (const AnimationClip &clip : animation->clips)
          for (float time : timesFor(clip)) {
            std::vector<NodePose> all, some;
            AnimationSystem::sample(*animation, clip, time, all);
            AnimationSystem::sample(*animation, clip, time, some, active.data());
            for (size_t i = 0; i < count; ++i) {
              if (!active[i])
                continue; // those are not posed: they hold the constant channels over the rest pose
              CHECK_EQ(some[i].translation, all[i].translation);
              CHECK(some[i].rotation == all[i].rotation);
              CHECK_EQ(some[i].scale, all[i].scale);
            }
          }
      }
    }
  });
}
