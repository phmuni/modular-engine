// Sampling glTF animation clips and crossfading between them.

#include "systems/animationSystem.h"

#include "components/animationStateMachine.h"
#include "components/animator.h"
#include "components/characterController.h"
#include "components/model.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/resources/skeletonPose.h"
#include "systems/animationGraph.h"
#include "systems/resourceSystem.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

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

// Moves a playback's clock on. A blend's `time` is the phase of the cycle, which goes at the pace of the mix of the two
// lengths; a single clip's is in seconds.
float stepClock(float time, float deltaTime, float speed, bool loop, const AnimationClip &clip,
                const AnimationClip *blendClip, float weight, bool &finished) {
  if (!blendClip)
    return advance(time, deltaTime * speed, clip.duration, loop, finished);
  const float mix = std::clamp(weight, 0.0f, 1.0f);
  const float duration = clip.duration + (blendClip->duration - clip.duration) * mix;
  return advance(time, duration > 0.0f ? deltaTime * speed / duration : 0.0f, 1.0f, loop, finished);
}

// The pose of a clip, or of two mixed by `weight` (then `time` is the phase)
void sampleClock(const MeshAnimation &animation, const AnimationClip &clip, const AnimationClip *blendClip,
                 float weight, float time, std::vector<NodePose> &pose, std::vector<NodePose> &scratch) {
  if (!blendClip) {
    AnimationSystem::sample(animation, clip, time, pose);
    return;
  }
  AnimationSystem::sample(animation, clip, time * clip.duration, pose);
  AnimationSystem::sample(animation, *blendClip, time * blendClip->duration, scratch);
  blendPoses(scratch, pose, std::clamp(weight, 0.0f, 1.0f));
  pose.swap(scratch);
}

const AnimationClip *findClip(const MeshAnimation &animation, const std::string &name) {
  if (animation.clips.empty())
    return nullptr;
  if (name.empty())
    return &animation.clips.front();
  const int index = animation.findClip(name);
  return index >= 0 ? &animation.clips[index] : nullptr;
}

// The two clips a blend state plays at this parameter value and how far toward the second (0 to 1): the clips at either
// side of the value, or one clip twice beyond the ends
void blendPair(const AnimationGraph::State &state, float value, const AnimationGraph::BlendPoint *&first,
               const AnimationGraph::BlendPoint *&second, float &weight) {
  const auto &points = state.blend;
  weight = 0.0f;
  if (value <= points.front().at) {
    first = second = &points.front();
  } else if (value >= points.back().at) {
    first = second = &points.back();
  } else {
    size_t upper = 0;
    while (points[upper].at <= value)
      ++upper;
    first = &points[upper - 1];
    second = &points[upper];
    weight = (value - first->at) / (second->at - first->at);
  }
}

float paramValue(const AnimationStateMachine &machine, const AnimationGraph &graph, const std::string &name) {
  auto value = machine.params.find(name);
  if (value != machine.params.end())
    return value->second;
  auto declared = graph.params.find(name);
  return declared != graph.params.end() ? declared->second.initial : 0.0f;
}

bool holds(const AnimationGraph::Condition &condition, float value) {
  using Op = AnimationGraph::Op;
  switch (condition.op) {
  case Op::Greater: return value > condition.value;
  case Op::Less: return value < condition.value;
  case Op::GreaterEqual: return value >= condition.value;
  case Op::LessEqual: return value <= condition.value;
  case Op::Equal: return std::fabs(value - condition.value) < 1e-5f;
  case Op::NotEqual: return std::fabs(value - condition.value) >= 1e-5f;
  case Op::IsTrue:
  case Op::Triggered: return value != 0.0f;
  case Op::IsFalse: return value == 0.0f;
  }
  return false;
}

int stepStates(const AnimationGraph::StateSet &set, int current, float &stateTime, const AnimationStateMachine &machine,
               const AnimationGraph &graph, const MeshAnimation *animation, float deltaTime,
               std::vector<std::string> &used, float &fade) {
  const AnimationGraph::State &state = set.states[current];
  stateTime += deltaTime * std::fabs(state.speed);

  // How long the state's clip is now (a blend's is the mix of its two), to measure exit times by
  float duration = 0.0f;
  const AnimationGraph::BlendPoint *first = nullptr, *second = nullptr;
  float weight = 0.0f;
  if (!state.blend.empty())
    blendPair(state, paramValue(machine, graph, state.blendParam), first, second, weight);
  if (animation && !state.empty) {
    const int a = animation->findClip(first ? first->clip : state.clip);
    const int b = second ? animation->findClip(second->clip) : a;
    if (a >= 0 && b >= 0)
      duration = animation->clips[a].duration + (animation->clips[b].duration - animation->clips[a].duration) * weight;
  }
  // Without a clip to measure, a wait for the end of it is over
  const float played = duration > 0.0f ? stateTime / duration : 1.0e9f;

  for (const AnimationGraph::Transition &transition : set.transitions) {
    if (transition.from != -1 && transition.from != current)
      continue;
    if (transition.from == -1 && transition.to == current)
      continue; // "any state" does not restart the state it is in
    // A transition with nothing to wait for leaves when the clip has played once, not at once
    const float exitTime = transition.exitTime < 0.0f && transition.when.empty() ? 1.0f : transition.exitTime;
    if (exitTime >= 0.0f && played < exitTime)
      continue;
    bool ready = true;
    for (const AnimationGraph::Condition &condition : transition.when)
      ready = ready && holds(condition, paramValue(machine, graph, condition.param));
    if (!ready)
      continue;
    for (const AnimationGraph::Condition &condition : transition.when)
      if (condition.op == AnimationGraph::Op::Triggered)
        used.push_back(condition.param);
    fade = transition.fade;
    return transition.to;
  }
  return -1;
}

// Points the playback at what a blend state plays at the parameter's value now
void driveState(Animator &playback, const AnimationGraph::State &state, const AnimationStateMachine &machine,
                const AnimationGraph &graph) {
  playback.speed = state.speed;
  playback.loop = state.loop;
  if (state.blend.empty())
    return;
  const AnimationGraph::BlendPoint *first = nullptr, *second = nullptr;
  float weight = 0.0f;
  blendPair(state, paramValue(machine, graph, state.blendParam), first, second, weight);
  playback.clip = first->clip;
  playback.blendClip = second->clip;
  playback.blendWeight = weight;
}

// The layer's own clock of presence: moves toward where the state it is in wants it
void movePresence(AnimationStateMachine::LayerState &layer, float deltaTime) {
  const float step = layer.presenceRate * deltaTime;
  if (layer.presence < layer.presenceTarget)
    layer.presence = std::min(layer.presenceTarget, layer.presence + step);
  else
    layer.presence = std::max(layer.presenceTarget, layer.presence - step);
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
  // State machines first, so the clip they pick is the one posed this frame. In a stable order, for repeatable runs.
  reloadChangedGraphs();
  std::vector<Entity> machines;
  m_componentManager.forEachComponent<AnimationStateMachine>(
      [&](Entity entity, AnimationStateMachine &) { machines.push_back(entity); });
  std::sort(machines.begin(), machines.end());
  for (Entity entity : machines)
    if (AnimationStateMachine *machine = m_componentManager.tryGet<AnimationStateMachine>(entity))
      runMachine(entity, *machine, deltaTime);

  // Joints of entities that are gone, or no longer animated, are dropped below, and so are the poses kept for fades
  // nobody used this frame
  for (auto &[entity, joints] : m_joints)
    joints.clear();
  for (auto &[key, snapshot] : m_snapshots)
    snapshot.seen = false;

  std::vector<NodePose> pose;
  m_componentManager.forEachComponent<Animator>([&](Entity entity, Animator &animator) {
    const Model *model = m_componentManager.tryGet<Model>(entity);
    const MeshAnimation *animation = model ? m_resourceSystem.getMesh(model->meshHandle).getAnimation() : nullptr;
    if (!animation)
      return;

    AnimationStateMachine *machine = m_componentManager.tryGet<AnimationStateMachine>(entity);
    std::shared_ptr<const AnimationGraph> layered;
    if (machine && !machine->layers.empty())
      layered = graph(machine->path);
    const bool hasLayers = layered && !layered->layers.empty();

    // The base layer; a clip the model lacks leaves the rest pose, which layers still go over
    if (!evaluate(entity, 0, animator, *animation, deltaTime, true, pose)) {
      if (!hasLayers) {
        m_joints[entity] = animation->restJointMatrices;
        return;
      }
      pose = SkeletonPose::rest(animation->skeleton);
    }
    if (hasLayers)
      applyLayers(entity, *machine, layered, model->meshHandle, *animation, deltaTime, pose);

    SkeletonPose::jointMatrices(animation->skeleton, pose, m_globals, m_joints[entity]);
  });

  for (auto it = m_joints.begin(); it != m_joints.end();)
    it = it->second.empty() ? m_joints.erase(it) : std::next(it);
  for (auto it = m_snapshots.begin(); it != m_snapshots.end();)
    it = it->second.seen ? std::next(it) : m_snapshots.erase(it);
}

bool AnimationSystem::evaluate(Entity entity, int slot, Animator &a, const MeshAnimation &animation, float deltaTime,
                               bool wanted, std::vector<NodePose> &pose) {
  const AnimationClip *clip = findClip(animation, a.clip);
  if (!clip)
    return false;
  const AnimationClip *blendClip = a.blendClip.empty() ? nullptr : findClip(animation, a.blendClip);

  if (a.playing) {
    bool finished = false;
    a.time = stepClock(a.time, deltaTime, a.speed, a.loop, *clip, blendClip, a.blendWeight, finished);
    if (finished)
      a.playing = false;
  }
  if (wanted)
    sampleClock(animation, *clip, blendClip, a.blendWeight, a.time, pose, m_scratch);

  // Fading out what it switched from: that keeps playing as it was (its own speed and loop, a blend in whole), or is a
  // frozen pose, and the two are mixed by how far the fade is
  if (!a.fadeFrom.empty() && a.fadeDuration > 0.0f) {
    const uint64_t key = snapshotKey(entity, slot);
    a.fadeElapsed += deltaTime;
    const float mix = std::clamp(a.fadeElapsed / a.fadeDuration, 0.0f, 1.0f);
    auto kept = m_snapshots.find(key);
    if (a.fadeFromFrozen) {
      if (kept != m_snapshots.end() && !kept->second.frozen.empty()) {
        if (wanted)
          blendPoses(pose, kept->second.frozen, mix);
      } else {
        a.fadeElapsed = a.fadeDuration; // the pose is gone (the playback was not looked at): no fade to run
      }
    } else if (const AnimationClip *from = findClip(animation, a.fadeFrom)) {
      const AnimationClip *fromBlend = a.fadeFromBlend.empty() ? nullptr : findClip(animation, a.fadeFromBlend);
      bool fromFinished = false;
      a.fadeFromTime = stepClock(a.fadeFromTime, deltaTime, a.fadeFromSpeed, a.fadeFromLoop, *from, fromBlend,
                                 a.fadeFromBlendWeight, fromFinished);
      if (wanted) {
        sampleClock(animation, *from, fromBlend, a.fadeFromBlendWeight, a.fadeFromTime, m_fadePose, m_scratch);
        blendPoses(pose, m_fadePose, mix);
      }
    }
    if (a.fadeElapsed >= a.fadeDuration) {
      a.fadeFrom.clear();
      a.fadeFromBlend.clear();
      a.fadeFromFrozen = false;
      m_snapshots.erase(key);
    } else if (wanted) {
      // What is shown now is where an interrupting fade starts from
      Snapshot &snapshot = m_snapshots[key];
      snapshot.last = pose;
      snapshot.seen = true;
    } else if (auto still = m_snapshots.find(key); still != m_snapshots.end()) {
      still->second.seen = true;
    }
  }
  return true;
}

const std::vector<float> &AnimationSystem::maskWeights(const std::shared_ptr<const AnimationGraph> &graph,
                                                       size_t layerIndex, uint32_t meshHandle,
                                                       const MeshAnimation &animation) {
  static const std::vector<float> wholeSkeleton;
  const AnimationGraph::Layer &layer = graph->layers[layerIndex];
  if (!layer.masked)
    return wholeSkeleton;
  const std::vector<SkeletonNode> &nodes = animation.skeleton.nodes;
  const MaskKey key{graph.get(), layerIndex, meshHandle};
  auto cached = m_masks.find(key);
  if (cached != m_masks.end() && cached->second.weights.size() == nodes.size())
    return cached->second.weights;

  // The weight a listed node was given; the others take their parent's, since parents come first
  std::vector<float> listed(nodes.size(), -1.0f);
  for (const auto &[name, weight] : layer.mask) {
    bool found = false;
    for (size_t i = 0; i < nodes.size(); ++i)
      if (nodes[i].name == name) {
        listed[i] = weight;
        found = true;
      }
    if (!found)
      LOG_W("[Animation] layer \"%s\": the mask names \"%s\", which the model %s does not have", layer.name.c_str(),
            name.c_str(), m_resourceSystem.getMeshPath(meshHandle).c_str());
  }
  CachedMask entry;
  entry.graph = graph;
  entry.weights.assign(nodes.size(), 0.0f);
  for (int node : animation.skeleton.order) {
    const int parent = nodes[node].parent;
    entry.weights[node] = listed[node] >= 0.0f ? listed[node] : parent >= 0 ? entry.weights[parent] : 0.0f;
  }
  return (m_masks[key] = std::move(entry)).weights;
}

void AnimationSystem::applyLayers(Entity entity, AnimationStateMachine &machine,
                                  const std::shared_ptr<const AnimationGraph> &graph, uint32_t meshHandle,
                                  const MeshAnimation &animation, float deltaTime, std::vector<NodePose> &pose) {
  for (size_t i = 0; i < graph->layers.size() && i < machine.layers.size(); ++i) {
    const AnimationGraph::Layer &layer = graph->layers[i];
    AnimationStateMachine::LayerState &state = machine.layers[i];
    if (state.name != layer.name)
      continue; // the definition changed since the machine last ran
    const float weight = layer.weightParam.empty()
                             ? layer.weight
                             : std::clamp(paramValue(machine, *graph, layer.weightParam), 0.0f, 1.0f);
    state.effectiveWeight = weight * state.presence;
    // A layer that shows nothing is not sampled, but its clocks go on
    const bool wanted = state.effectiveWeight > 0.0f;
    const int slot = static_cast<int>(i) + 1;
    const bool played = evaluate(entity, slot, state.playback, animation, deltaTime, wanted, m_layerPose);
    if (!played || !wanted)
      continue;
    const std::vector<float> &mask = maskWeights(graph, i, meshHandle, animation);

    if (!layer.additive) {
      for (size_t node = 0; node < pose.size() && node < m_layerPose.size(); ++node) {
        const float w = state.effectiveWeight * (mask.empty() ? 1.0f : mask[node]);
        if (w <= 0.0f)
          continue;
        pose[node].translation = glm::mix(pose[node].translation, m_layerPose[node].translation, w);
        pose[node].rotation = glm::slerp(pose[node].rotation, m_layerPose[node].rotation, w);
        pose[node].scale = glm::mix(pose[node].scale, m_layerPose[node].scale, w);
      }
      continue;
    }

    // Additive: what the clip does beyond its reference pose, added to what is below. The reference is the first frame
    // of the state's `reference` clip, or of the clip itself (of both clips of a blend, mixed like the blend).
    const Animator &playback = state.playback;
    const AnimationClip *referenceClip = state.reference.empty() ? nullptr : findClip(animation, state.reference);
    if (referenceClip)
      sampleClock(animation, *referenceClip, nullptr, 0.0f, 0.0f, m_referencePose, m_scratch);
    else if (const AnimationClip *clip = findClip(animation, playback.clip))
      sampleClock(animation, *clip, playback.blendClip.empty() ? nullptr : findClip(animation, playback.blendClip),
                  playback.blendWeight, 0.0f, m_referencePose, m_scratch);
    else
      continue;
    for (size_t node = 0; node < pose.size() && node < m_layerPose.size() && node < m_referencePose.size(); ++node) {
      const float w = state.effectiveWeight * (mask.empty() ? 1.0f : mask[node]);
      if (w <= 0.0f)
        continue;
      const NodePose &current = m_layerPose[node], &reference = m_referencePose[node];
      pose[node].translation += w * (current.translation - reference.translation);
      // The change of rotation, in the node's own space, applied after what is below
      const glm::quat change = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                          glm::inverse(reference.rotation) * current.rotation, w);
      pose[node].rotation = glm::normalize(pose[node].rotation * change);
      for (int axis = 0; axis < 3; ++axis) {
        const float ratio = reference.scale[axis] != 0.0f ? current.scale[axis] / reference.scale[axis] : 1.0f;
        pose[node].scale[axis] *= 1.0f + w * (ratio - 1.0f);
      }
    }
  }
}

void AnimationSystem::crossfade(Entity entity, Animator &animator, const MeshAnimation *animation,
                                const std::string &clip, float fadeSeconds, bool loop, int layer) {
  const bool previousLoop = animator.loop;
  animator.loop = loop;
  animator.playing = true;
  const uint64_t key = snapshotKey(entity, layer);
  if (fadeSeconds > 0.0f) {
    auto kept = m_snapshots.find(key);
    const bool fading = !animator.fadeFrom.empty();
    if (fading && animator.fadeElapsed <= 0.0f) {
      // Nothing of the fade running has been on screen yet: what it fades out stays, with the new clip in its place
    } else if (fading && kept != m_snapshots.end() && !kept->second.last.empty()) {
      // Interrupting a fade: the pose on screen is what fades out, not a clip's own pose
      kept->second.frozen = kept->second.last;
      animator.fadeFrom = animator.clip.empty() ? std::string("(frozen)") : animator.clip; // only to tell it is on
      animator.fadeFromBlend.clear();
      animator.fadeFromFrozen = true;
    } else {
      // What is on screen now is what it fades out, a blend of two clips whole, with its own speed and loop
      std::string from = animator.clip;
      if (from.empty() && animation && !animation->clips.empty())
        from = animation->clips[0].name;
      animator.fadeFrom = from;
      animator.fadeFromTime = animator.time;
      animator.fadeFromBlend = animator.blendClip;
      animator.fadeFromBlendWeight = animator.blendWeight;
      animator.fadeFromSpeed = animator.speed;
      animator.fadeFromLoop = previousLoop;
      animator.fadeFromFrozen = false;
      m_snapshots.erase(key);
    }
    animator.fadeElapsed = 0.0f;
    animator.fadeDuration = fadeSeconds;
  } else {
    animator.fadeFrom.clear();
    animator.fadeFromBlend.clear();
    animator.fadeFromFrozen = false;
    m_snapshots.erase(key);
  }
  animator.blendClip.clear();
  animator.blendWeight = 0.0f;
  animator.clip = clip;
  animator.time = 0.0f;
}

// --- State machines ---------------------------------------------------------------------------------------------

const MeshAnimation *AnimationSystem::animationOf(Entity entity) const {
  const Model *model = m_componentManager.tryGet<Model>(entity);
  return model ? m_resourceSystem.getMesh(model->meshHandle).getAnimation() : nullptr;
}

std::shared_ptr<const AnimationGraph> AnimationSystem::graph(const std::string &path) {
  if (path.empty())
    return nullptr;
  auto it = m_graphs.find(path);
  if (it != m_graphs.end())
    return it->second.graph;

  CachedGraph cached;
  const std::string file = EngineConfig::resolveAsset(path);
  std::error_code error;
  cached.exists = std::filesystem::exists(file, error);
  if (!cached.exists) {
    LOG_E("[Animation] %s: file not found", path.c_str());
    return m_graphs.emplace(path, cached).first->second.graph;
  }
  cached.modified = std::filesystem::last_write_time(file, error);
  std::ifstream in(file);
  const nlohmann::json data = nlohmann::json::parse(in, nullptr, false);
  auto parsed = std::make_shared<AnimationGraph>();
  std::string reason;
  if (data.is_discarded())
    LOG_E("[Animation] %s: not valid JSON", path.c_str());
  else if (!AnimationGraph::parse(data, *parsed, reason))
    LOG_E("[Animation] %s: %s", path.c_str(), reason.c_str());
  else
    cached.graph = parsed;
  return m_graphs.emplace(path, cached).first->second.graph;
}

void AnimationSystem::reloadChangedGraphs() {
  // A couple of times a second: a file edited while the game runs (or the editor is open) is read again
  if (++m_graphCheck < 30)
    return;
  m_graphCheck = 0;
  std::vector<std::string> changed;
  for (auto &[path, cached] : m_graphs) {
    std::error_code error;
    const std::string file = EngineConfig::resolveAsset(path);
    const bool exists = std::filesystem::exists(file, error);
    if (exists != cached.exists || (exists && std::filesystem::last_write_time(file, error) != cached.modified))
      changed.push_back(path);
  }
  for (const std::string &path : changed) {
    m_graphs.erase(path);
    graph(path);
  }
  // Masks worked out for a definition that was replaced are of no use now
  for (auto it = m_masks.begin(); it != m_masks.end();)
    it = it->second.graph.use_count() <= 1 ? m_masks.erase(it) : std::next(it);
}

std::vector<AnimationSystem::StateChange> AnimationSystem::takeStateChanges() {
  std::vector<StateChange> changes;
  changes.swap(m_changes);
  return changes;
}

void AnimationSystem::startMachine(Entity entity) {
  AnimationStateMachine *machine = m_componentManager.tryGet<AnimationStateMachine>(entity);
  if (machine && machine->state.empty())
    runMachine(entity, *machine, 0.0f);
}

void AnimationSystem::restartMachine(Entity entity) {
  AnimationStateMachine *machine = m_componentManager.tryGet<AnimationStateMachine>(entity);
  if (!machine)
    return;
  machine->state.clear();
  machine->params.clear();
  machine->manual = false;
  machine->layers.clear();
  runMachine(entity, *machine, 0.0f);
}

void AnimationSystem::enterState(Entity entity, AnimationStateMachine &machine, const AnimationGraph &graph, int index,
                                 float fadeSeconds) {
  const AnimationGraph::State &state = graph.base.states[index];
  Animator *animator = m_componentManager.tryGet<Animator>(entity);
  if (!animator) {
    animator = &m_componentManager.addInPlace<Animator>(entity);
    fadeSeconds = 0.0f; // nothing to fade from
  }
  const MeshAnimation *animation = animationOf(entity);
  const std::string &clip = state.blend.empty() ? state.clip : state.blend.front().clip;
  if (animation && animation->findClip(clip) < 0)
    LOG_W("[Animation] %s: state \"%s\" plays the clip \"%s\", which entity %d's model does not have",
          machine.path.c_str(), state.name.c_str(), clip.c_str(), entity);
  crossfade(entity, *animator, animation, clip, fadeSeconds, state.loop);
  animator->speed = state.speed;
  machine.state = state.name;
  machine.stateTime = 0.0f;
  machine.manual = false;
}

void AnimationSystem::enterLayerState(Entity entity, AnimationStateMachine &machine,
                                      const AnimationGraph::Layer &layer, int layerIndex, int index,
                                      float fadeSeconds) {
  AnimationStateMachine::LayerState &runtime = machine.layers[layerIndex];
  const AnimationGraph::State &state = layer.states[index];
  runtime.state = state.name;
  runtime.stateTime = 0.0f;
  runtime.presenceTarget = state.empty ? 0.0f : 1.0f;
  runtime.presenceRate = fadeSeconds > 0.0f ? 1.0f / fadeSeconds : 0.0f;
  if (fadeSeconds <= 0.0f)
    runtime.presence = runtime.presenceTarget;
  if (state.empty)
    return; // the clip it played goes on while the layer fades out

  const MeshAnimation *animation = animationOf(entity);
  const std::string &clip = state.blend.empty() ? state.clip : state.blend.front().clip;
  if (animation && animation->findClip(clip) < 0)
    LOG_W("[Animation] %s: state \"%s\" of layer \"%s\" plays the clip \"%s\", which entity %d's model does not have",
          machine.path.c_str(), state.name.c_str(), layer.name.c_str(), clip.c_str(), entity);
  // From nothing the new clip is all there is: the layer comes in by presence, not by a mix with what it last played
  const float mix = runtime.presence <= 0.0f ? 0.0f : fadeSeconds;
  crossfade(entity, runtime.playback, animation, clip, mix, state.loop, layerIndex + 1);
  runtime.playback.speed = state.speed;
  runtime.reference = state.reference;
}

void AnimationSystem::runMachine(Entity entity, AnimationStateMachine &machine, float deltaTime) {
  const std::shared_ptr<const AnimationGraph> held = graph(machine.path);
  if (!held)
    return;
  const AnimationGraph &graph = *held;

  int current = graph.base.findState(machine.state);
  if (current < 0) {
    // Starting (or the file changed and lost the state): the parameters it declares, then the default state
    for (auto it = machine.params.begin(); it != machine.params.end();)
      it = graph.params.count(it->first) ? std::next(it) : machine.params.erase(it);
    for (const auto &[name, param] : graph.params)
      machine.params.emplace(name, param.initial);
    current = graph.base.defaultState;
    enterState(entity, machine, graph, current, 0.0f);
  }

  // The layers the file has, matched to those running by name: new ones start in their default state, a layer whose
  // state the file lost starts over, and the ones the file dropped go
  bool same = machine.layers.size() == graph.layers.size();
  for (size_t i = 0; same && i < graph.layers.size(); ++i)
    same = machine.layers[i].name == graph.layers[i].name;
  if (!same) {
    std::vector<AnimationStateMachine::LayerState> running;
    for (const AnimationGraph::Layer &layer : graph.layers) {
      auto known = std::find_if(machine.layers.begin(), machine.layers.end(),
                                [&](const AnimationStateMachine::LayerState &r) { return r.name == layer.name; });
      running.push_back(known != machine.layers.end() ? std::move(*known) : AnimationStateMachine::LayerState());
      running.back().name = layer.name;
    }
    machine.layers = std::move(running);
  }
  for (size_t i = 0; i < graph.layers.size(); ++i) {
    const AnimationGraph::Layer &layer = graph.layers[i];
    if (layer.findState(machine.layers[i].state) < 0) {
      machine.layers[i] = AnimationStateMachine::LayerState();
      machine.layers[i].name = layer.name;
      enterLayerState(entity, machine, layer, static_cast<int>(i), layer.defaultState, 0.0f);
    }
  }

  // Parameters fed by the physics body
  if (const CharacterController *character = m_componentManager.tryGet<CharacterController>(entity)) {
    for (const auto &[name, param] : graph.params) {
      switch (param.source) {
      case AnimationGraph::Source::Speed:
        machine.params[name] = std::hypot(character->velocity.x, character->velocity.z);
        break;
      case AnimationGraph::Source::VerticalSpeed: machine.params[name] = character->velocity.y; break;
      case AnimationGraph::Source::Grounded: machine.params[name] = character->onGround ? 1.0f : 0.0f; break;
      case AnimationGraph::Source::None: break;
      }
    }
  }
  if (deltaTime <= 0.0f)
    return;

  // The base and then each layer look for a transition to take; a trigger is used up only once all of them have, so
  // layers waiting for the same trigger all react to it
  const MeshAnimation *animation = animationOf(entity);
  std::vector<std::string> used;
  float fade = 0.0f;
  int next = stepStates(graph.base, current, machine.stateTime, machine, graph, animation, deltaTime, used, fade);
  if (next >= 0) {
    const std::string from = machine.state;
    enterState(entity, machine, graph, next, fade);
    m_changes.push_back({entity, AnimationGraph::kBaseLayer, from, machine.state});
    current = next;
  }
  for (size_t i = 0; i < graph.layers.size(); ++i) {
    const AnimationGraph::Layer &layer = graph.layers[i];
    AnimationStateMachine::LayerState &runtime = machine.layers[i];
    const int at = layer.findState(runtime.state);
    next = stepStates(layer, at, runtime.stateTime, machine, graph, animation, deltaTime, used, fade);
    if (next < 0)
      continue;
    const std::string from = runtime.state;
    enterLayerState(entity, machine, layer, static_cast<int>(i), next, fade);
    m_changes.push_back({entity, layer.name, from, runtime.state});
  }
  for (const std::string &name : used)
    machine.params[name] = 0.0f;

  for (size_t i = 0; i < graph.layers.size(); ++i) {
    AnimationStateMachine::LayerState &runtime = machine.layers[i];
    movePresence(runtime, deltaTime);
    const AnimationGraph::State &state = graph.layers[i].states[graph.layers[i].findState(runtime.state)];
    if (!state.empty)
      driveState(runtime.playback, state, machine, graph);
  }

  // Drive the Animator with the state, unless a manual play took it over
  if (machine.manual)
    return;
  if (Animator *animator = m_componentManager.tryGet<Animator>(entity))
    driveState(*animator, graph.base.states[current], machine, graph);
}

const std::vector<glm::mat4> *AnimationSystem::getJointMatrices(Entity entity) const {
  auto it = m_joints.find(entity);
  if (it != m_joints.end())
    return &it->second;
  const Model *model = m_componentManager.tryGet<Model>(entity);
  const MeshAnimation *animation = model ? m_resourceSystem.getMesh(model->meshHandle).getAnimation() : nullptr;
  return animation ? &animation->restJointMatrices : nullptr;
}
