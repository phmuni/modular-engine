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

// Mixes into `into` what is `from`, `weightOfInto` of the way to `into`; only the nodes `active` marks (all when null)
void blendPoses(std::vector<NodePose> &into, const std::vector<NodePose> &from, float weightOfInto,
                const uint8_t *active = nullptr) {
  for (size_t i = 0; i < into.size() && i < from.size(); ++i) {
    if (active && !active[i])
      continue;
    into[i].translation = glm::mix(from[i].translation, into[i].translation, weightOfInto);
    into[i].rotation = glm::slerp(from[i].rotation, into[i].rotation, weightOfInto);
    into[i].scale = glm::mix(from[i].scale, into[i].scale, weightOfInto);
  }
}

// Moves `time` on by `delta` within the clip: around when looping, stopping at an end otherwise
float advance(float time, float delta, float duration, bool loop, bool &finished) {
  finished = false;
  if (delta == 0.0f)
    return time; // standing still (a pause, speed 0) ends nothing, not even at the clip's very start
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

// How long one cycle of a clip, or of the blend of two, takes, in seconds
float cycleLength(const AnimationClip &clip, const AnimationClip *blendClip, float weight) {
  if (!blendClip)
    return clip.duration;
  return clip.duration + (blendClip->duration - clip.duration) * std::clamp(weight, 0.0f, 1.0f);
}

// Where a playback's clock is in the cycle, 0 to 1 (a blend's `time` already is)
float cycleShare(float time, const AnimationClip &clip, const AnimationClip *blendClip) {
  return blendClip ? time : clip.duration > 0.0f ? time / clip.duration : 0.0f;
}

// The pose of a clip, or of two mixed by `weight` (then `time` is the phase)
void sampleClock(const MeshAnimation &animation, const AnimationClip &clip, const AnimationClip *blendClip,
                 float weight, float time, std::vector<NodePose> &pose, std::vector<NodePose> &scratch,
                 const uint8_t *active = nullptr) {
  if (!blendClip) {
    AnimationSystem::sample(animation, clip, time, pose, active);
    return;
  }
  AnimationSystem::sample(animation, clip, time * clip.duration, pose, active);
  AnimationSystem::sample(animation, *blendClip, time * blendClip->duration, scratch, active);
  blendPoses(scratch, pose, std::clamp(weight, 0.0f, 1.0f), active);
  pose.swap(scratch);
}

// Writes a clip's constant channels over a pose
void layConstants(const AnimationClip &clip, std::vector<NodePose> &pose) {
  for (const ConstantChannel &constant : clip.constants) {
    if (constant.node < 0 || constant.node >= static_cast<int>(pose.size()))
      continue;
    NodePose &node = pose[constant.node];
    if (constant.path == AnimationChannel::Path::Rotation)
      node.rotation = glm::quat(constant.value.w, constant.value.x, constant.value.y, constant.value.z);
    else if (constant.path == AnimationChannel::Path::Translation)
      node.translation = glm::vec3(constant.value);
    else
      node.scale = glm::vec3(constant.value);
  }
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
                             std::vector<NodePose> &pose, const uint8_t *active) {
  // The clip's base (the rest pose with its constant channels) is copied over what is there, with no new buffer; the
  // channels that change are then written over it
  const size_t index = static_cast<size_t>(&clip - animation.clips.data());
  if (index < animation.clips.size()) {
    if (animation.clipBases.size() != animation.clips.size())
      animation.clipBases.resize(animation.clips.size());
    std::vector<NodePose> &base = animation.clipBases[index];
    if (base.size() != animation.skeleton.nodes.size()) {
      base = animation.restPose.size() == animation.skeleton.nodes.size() ? animation.restPose
                                                                          : SkeletonPose::rest(animation.skeleton);
      layConstants(clip, base);
    }
    pose = base;
  } else {
    pose = SkeletonPose::rest(animation.skeleton);
    layConstants(clip, pose);
  }
  const int nodeCount = static_cast<int>(pose.size());
  for (const AnimationTrack &track : clip.tracks) {
    // Where `time` falls is the same for every channel of the track, so it is looked for once, when one is wanted
    bool located = false;
    size_t key = 0, next = 0;
    float blend = 0.0f, interval = 0.0f;
    for (size_t c = track.firstChannel; c < track.firstChannel + track.channelCount; ++c) {
      const AnimationChannel &channel = clip.channels[c];
      if (channel.node < 0 || channel.node >= nodeCount || (active && !active[channel.node]))
        continue;
      if (!located) {
        locate(track.times, time, key, blend, interval);
        next = std::min(key + 1, track.times.size() - 1);
        located = true;
      }
      NodePose &node = pose[channel.node];
      const bool isRotation = channel.path == AnimationChannel::Path::Rotation;
      glm::vec4 value;
      switch (channel.interpolation) {
      case AnimationChannel::Interpolation::Step:
        value = channel.values[key];
        if (isRotation) {
          node.rotation = glm::quat(value.w, value.x, value.y, value.z); // unit since it was added
          continue;
        }
        break;
      case AnimationChannel::Interpolation::Linear:
        if (isRotation) {
          const glm::vec4 &a = channel.values[key];
          if (blend == 0.0f) {
            node.rotation = glm::quat(a.w, a.x, a.y, a.z);
            continue;
          }
          const glm::vec4 &b = channel.values[next];
          const glm::vec3 arc = channel.arcs[key];
          glm::vec4 r;
          if (arc.x == 0.0f) {
            r = glm::normalize(glm::mix(a, b, blend));
          } else {
            // sin((1-t)w) / sin(w) = cos(tw) - cot(w) sin(tw): one sine and one cosine of the same angle
            const float angle = blend * arc.x;
            const float s = std::sin(angle), c = std::cos(angle);
            r = a * (c - arc.z * s) + b * (s * arc.y);
          }
          node.rotation = glm::quat(r.w, r.x, r.y, r.z);
          continue;
        }
        value = glm::mix(channel.values[key], channel.values[next], blend);
        break;
      case AnimationChannel::Interpolation::CubicSpline: {
        // Three values per key: in-tangent, value, out-tangent; tangents scale with the time between keys
        const glm::vec4 &p0 = channel.values[key * 3 + 1], &m0 = channel.values[key * 3 + 2];
        const glm::vec4 &p1 = channel.values[next * 3 + 1], &m1 = channel.values[next * 3];
        value = key == next ? p0 : hermite(p0, m0 * interval, p1, m1 * interval, blend);
        if (isRotation) {
          node.rotation = toQuat(value);
          continue;
        }
        break;
      }
      }
      if (channel.path == AnimationChannel::Path::Translation)
        node.translation = glm::vec3(value);
      else
        node.scale = glm::vec3(value);
    }
  }
}

void AnimationSystem::update(float deltaTime) {
  // State machines first, so the clip they pick is the one posed this frame. In a stable order, for repeatable runs.
  reloadChangedGraphs();
  // Windows whose machine is gone (the component removed, the entity destroyed) are closed
  for (auto &[entity, records] : m_windows)
    if (!m_componentManager.tryGet<AnimationStateMachine>(entity))
      for (OpenWindow &record : records)
        if (!record.closing) {
          record.closing = true;
          queueEvent(entity, record.name, AnimationEvent::Phase::End, record.layer, record.state);
        }
  std::vector<Entity> machines;
  m_componentManager.forEachComponent<AnimationStateMachine>(
      [&](Entity entity, AnimationStateMachine &) { machines.push_back(entity); });
  std::sort(machines.begin(), machines.end());
  m_running.clear();
  for (Entity entity : machines)
    if (AnimationStateMachine *machine = m_componentManager.tryGet<AnimationStateMachine>(entity)) {
      Running running{entity, nullptr, -1};
      running.state = runMachine(entity, *machine, deltaTime, &running.graph);
      m_running.push_back(std::move(running));
    }

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
    int baseState = -1;
    if (machine) {
      auto running = std::lower_bound(m_running.begin(), m_running.end(), entity,
                                      [](const Running &r, Entity e) { return r.entity < e; });
      if (running != m_running.end() && running->entity == entity) {
        layered = running->graph;
        baseState = running->state;
      }
    }
    const bool hasLayers = layered && !layered->layers.empty() && !machine->layers.empty();

    // What the base's clock is looked at for animation events with: the state it is in. A clip played by hand is not
    // the state's, so it is silent and its windows are closed.
    static const std::string baseLayer = AnimationGraph::kBaseLayer;
    EventSource baseEvents;
    if (layered && layered->hasEvents && baseState >= 0) {
      baseEvents = {&layered->base.states[baseState], &baseLayer, &machine->clockMoved, machine->manual};
      if (machine->manual && !m_windows.empty())
        closeWindows(entity, baseLayer);
    }

    // The base layer; a clip the model lacks leaves the rest pose, which layers still go over
    if (!evaluate(entity, 0, animator, *animation, deltaTime, true, pose, baseEvents.state ? &baseEvents : nullptr)) {
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
                               bool wanted, std::vector<NodePose> &pose, const EventSource *events,
                               const uint8_t *active) {
  const AnimationClip *clip = findClip(animation, a.clip);
  if (!clip)
    return false;
  const AnimationClip *blendClip = a.blendClip.empty() ? nullptr : findClip(animation, a.blendClip);

  if (a.playing) {
    bool finished = false;
    const float before = events ? cycleShare(a.time, *clip, blendClip) : 0.0f;
    const float length = events ? cycleLength(*clip, blendClip, a.blendWeight) : 0.0f;
    a.time = stepClock(a.time, deltaTime, a.speed, a.loop, *clip, blendClip, a.blendWeight, finished);
    if (finished)
      a.playing = false;
    // The events are the state's own, looked for where the clock of what is playing now went; what fades out is silent
    if (events && length > 0.0f)
      moveEvents(entity, *events, a.loop, before, cycleShare(a.time, *clip, blendClip),
                 deltaTime * a.speed / length);
  }
  if (wanted)
    sampleClock(animation, *clip, blendClip, a.blendWeight, a.time, pose, m_scratch, active);

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
          blendPoses(pose, kept->second.frozen, mix, active);
      } else {
        a.fadeElapsed = a.fadeDuration; // the pose is gone (the playback was not looked at): no fade to run
      }
    } else if (const AnimationClip *from = findClip(animation, a.fadeFrom)) {
      const AnimationClip *fromBlend = a.fadeFromBlend.empty() ? nullptr : findClip(animation, a.fadeFromBlend);
      bool fromFinished = false;
      a.fadeFromTime = stepClock(a.fadeFromTime, deltaTime, a.fadeFromSpeed, a.fadeFromLoop, *from, fromBlend,
                                 a.fadeFromBlendWeight, fromFinished);
      if (wanted) {
        sampleClock(animation, *from, fromBlend, a.fadeFromBlendWeight, a.fadeFromTime, m_fadePose, m_scratch, active);
        blendPoses(pose, m_fadePose, mix, active);
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

const AnimationSystem::Mask *AnimationSystem::maskOf(const std::shared_ptr<const AnimationGraph> &graph,
                                                     size_t layerIndex, uint32_t meshHandle,
                                                     const MeshAnimation &animation) {
  const AnimationGraph::Layer &layer = graph->layers[layerIndex];
  if (!layer.masked)
    return nullptr;
  const std::vector<SkeletonNode> &nodes = animation.skeleton.nodes;
  const MaskKey key{graph.get(), layerIndex, meshHandle};
  auto cached = m_masks.find(key);
  if (cached != m_masks.end() && cached->second.mask.weights.size() == nodes.size())
    return &cached->second.mask;

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
  entry.mask.weights.assign(nodes.size(), 0.0f);
  for (int node : animation.skeleton.order) {
    const int parent = nodes[node].parent;
    entry.mask.weights[node] = listed[node] >= 0.0f ? listed[node] : parent >= 0 ? entry.mask.weights[parent] : 0.0f;
  }
  entry.mask.active.resize(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i)
    entry.mask.active[i] = entry.mask.weights[i] > 0.0f;
  return &(m_masks[key] = std::move(entry)).mask;
}

const std::vector<NodePose> &AnimationSystem::firstFrame(const MeshAnimation &animation, const AnimationClip &clip) {
  const size_t index = static_cast<size_t>(&clip - animation.clips.data());
  if (animation.firstFrames.size() != animation.clips.size())
    animation.firstFrames.resize(animation.clips.size());
  std::vector<NodePose> &frame = animation.firstFrames[index];
  if (frame.empty())
    sample(animation, clip, 0.0f, frame);
  return frame;
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
    // A layer that shows nothing is silent, and what was open in it closes
    if (!wanted && !m_windows.empty())
      closeWindows(entity, layer.name);
    EventSource events;
    if (graph->hasEvents) {
      const int at = layer.findState(state.state);
      if (at >= 0)
        events = {&layer.states[at], &layer.name, &state.clockMoved, !wanted};
    }
    const Mask *mask = wanted ? maskOf(graph, i, meshHandle, animation) : nullptr;
    const uint8_t *active = mask ? mask->active.data() : nullptr;
    const bool played = evaluate(entity, slot, state.playback, animation, deltaTime, wanted, m_layerPose,
                                 events.state ? &events : nullptr, active);
    if (!played || !wanted)
      continue;
    const size_t count = std::min(pose.size(), m_layerPose.size());

    if (!layer.additive) {
      for (size_t node = 0; node < count; ++node) {
        if (active && !active[node])
          continue;
        const float w = state.effectiveWeight * (mask ? mask->weights[node] : 1.0f);
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
    const AnimationClip *first = referenceClip, *second = nullptr;
    float mixTo = 0.0f;
    if (!referenceClip) {
      first = findClip(animation, playback.clip);
      if (!first)
        continue;
      second = playback.blendClip.empty() ? nullptr : findClip(animation, playback.blendClip);
      mixTo = std::clamp(playback.blendWeight, 0.0f, 1.0f);
    }
    const std::vector<NodePose> &firstPose = firstFrame(animation, *first);
    const std::vector<NodePose> *secondPose = second ? &firstFrame(animation, *second) : nullptr;
    for (size_t node = 0; node < count && node < firstPose.size(); ++node) {
      if (active && !active[node])
        continue;
      const float w = state.effectiveWeight * (mask ? mask->weights[node] : 1.0f);
      if (w <= 0.0f)
        continue;
      const NodePose &current = m_layerPose[node];
      NodePose reference = firstPose[node];
      if (secondPose && node < secondPose->size()) {
        const NodePose &to = (*secondPose)[node];
        reference.translation = glm::mix(reference.translation, to.translation, mixTo);
        reference.rotation = glm::slerp(reference.rotation, to.rotation, mixTo);
        reference.scale = glm::mix(reference.scale, to.scale, mixTo);
      }
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

std::vector<AnimationSystem::PendingEvent> AnimationSystem::takeEvents() {
  std::vector<PendingEvent> events;
  events.swap(m_events);
  return events;
}

void AnimationSystem::queueEvent(Entity entity, const std::string &name, AnimationEvent::Phase phase,
                                 const std::string &layer, const std::string &state) {
  m_events.push_back({entity, AnimationEvent{name, phase, layer, state}});
}

AnimationSystem::OpenWindow *AnimationSystem::openWindow(Entity entity, const std::string &layer, int index) {
  auto records = m_windows.find(entity);
  if (records == m_windows.end())
    return nullptr;
  for (OpenWindow &record : records->second)
    if (!record.closing && record.index == index && record.layer == layer)
      return &record;
  return nullptr;
}

void AnimationSystem::announce(const PendingEvent &pending) {
  const AnimationEvent &event = pending.event;
  if (event.phase == AnimationEvent::Phase::Fire)
    return;
  auto records = m_windows.find(pending.entity);
  if (records == m_windows.end())
    return;
  auto &list = records->second;
  const bool begin = event.phase == AnimationEvent::Phase::Begin;
  for (auto it = list.begin(); it != list.end(); ++it) {
    if (it->name != event.name || it->layer != event.layer || it->state != event.state)
      continue;
    if (begin && !it->announced) {
      it->announced = true;
      return;
    }
    if (!begin && it->closing) {
      list.erase(it);
      if (list.empty())
        m_windows.erase(records);
      return;
    }
  }
}

bool AnimationSystem::inWindow(Entity entity, std::string_view name) const {
  auto records = m_windows.find(entity);
  if (records == m_windows.end())
    return false;
  for (const OpenWindow &record : records->second)
    if (record.announced && record.name == name)
      return true;
  return false;
}

std::vector<std::string> AnimationSystem::openWindows(Entity entity, const std::string &layer) const {
  std::vector<std::string> names;
  auto records = m_windows.find(entity);
  if (records != m_windows.end())
    for (const OpenWindow &record : records->second)
      if (record.announced && record.layer == layer)
        names.push_back(record.name);
  return names;
}

void AnimationSystem::closeWindows(Entity entity, std::string_view layer) {
  auto records = m_windows.find(entity);
  if (records == m_windows.end())
    return;
  for (OpenWindow &record : records->second)
    if (!record.closing && record.layer == layer) {
      record.closing = true;
      queueEvent(entity, record.name, AnimationEvent::Phase::End, record.layer, record.state);
    }
}

void AnimationSystem::closeAllWindows(Entity entity) {
  auto records = m_windows.find(entity);
  if (records == m_windows.end())
    return;
  for (OpenWindow &record : records->second)
    if (!record.closing) {
      record.closing = true;
      queueEvent(entity, record.name, AnimationEvent::Phase::End, record.layer, record.state);
    }
}

void AnimationSystem::validateWindows(Entity entity, const AnimationStateMachine &machine,
                                      const AnimationGraph &graph) {
  auto records = m_windows.find(entity);
  if (records == m_windows.end())
    return;
  for (OpenWindow &record : records->second) {
    if (record.closing)
      continue;
    // The state the layer is in now, and whether the window is still one of its events
    const AnimationGraph::StateSet *set = &graph.base;
    const std::string *current = &machine.state;
    if (record.layer != AnimationGraph::kBaseLayer) {
      set = nullptr;
      for (size_t i = 0; i < graph.layers.size() && i < machine.layers.size(); ++i)
        if (graph.layers[i].name == record.layer && machine.layers[i].name == record.layer) {
          set = &graph.layers[i];
          current = &machine.layers[i].state;
        }
    }
    bool valid = false;
    if (set && *current == record.state) {
      const int state = set->findState(record.state);
      if (state >= 0) {
        const auto &events = set->states[state].events;
        valid = record.index >= 0 && record.index < static_cast<int>(events.size()) &&
                events[record.index].window && events[record.index].name == record.name;
      }
    }
    if (!valid) {
      record.closing = true;
      queueEvent(entity, record.name, AnimationEvent::Phase::End, record.layer, record.state);
    }
  }
}

// Looks at what the clock of a playback crossed in this frame, from `before` to `after` (shares of the cycle) with
// `travel` the signed share it was asked to move (more than 1 when a loop goes round more than once), and queues the
// events in the order they were crossed. A moment is crossed when the clock reaches it, a window opens when the clock
// reaches its start and closes when it reaches its end (the other way round going backwards); the clip's very start
// counts as reached by the first move of a state, so a moment at 0 is heard (the end of a clip before it is not).
void AnimationSystem::moveEvents(Entity entity, const EventSource &source, bool loop, float before, float after,
                                 float travel) {
  if (travel == 0.0f)
    return;
  const bool first = !*source.clockMoved;
  float from = loop ? before - std::floor(before) : std::clamp(before, 0.0f, 1.0f);
  // Where the clock ended in unwrapped terms, kept to the position it was really left at: the next frame starts there
  float to = after;
  if (loop)
    to += std::round(from + travel - after);
  if (to == from)
    return;
  *source.clockMoved = true;
  if (source.muted)
    return;
  const std::vector<AnimationGraph::Event> &events = source.state->events;
  if (events.empty() && m_windows.empty())
    return;
  constexpr float kMostLaps = 256.0f; // a frame of absurd length hears what the last laps would
  if (std::fabs(to - from) > kMostLaps)
    from = to - std::copysign(kMostLaps, to - from);
  const bool forward = to > from;
  const float low = std::min(from, to), high = std::max(from, to);
  const std::string &layer = *source.layer;

  struct Moment {
    float position;
    int event;
    AnimationEvent::Phase phase;
  };
  std::vector<Moment> moments;
  auto crossing = [&](float share, int event, AnimationEvent::Phase phase) {
    const int firstLap = loop ? static_cast<int>(std::floor(low - share)) : 0;
    const int lastLap = loop ? static_cast<int>(std::ceil(high - share)) : 0;
    for (int lap = firstLap; lap <= lastLap; ++lap) {
      const float position = static_cast<float>(lap) + share;
      const bool reached = forward ? position > from && position <= to : position >= to && position < from;
      if (reached || (first && share == 0.0f && position == from))
        moments.push_back({position, event, phase});
    }
  };
  using Phase = AnimationEvent::Phase;
  for (size_t i = 0; i < events.size(); ++i) {
    const AnimationGraph::Event &event = events[i];
    const int index = static_cast<int>(i);
    if (!event.window) {
      crossing(event.at, index, Phase::Fire);
    } else {
      crossing(event.from, index, forward ? Phase::Begin : Phase::End);
      crossing(event.to, index, forward ? Phase::End : Phase::Begin);
    }
  }
  std::stable_sort(moments.begin(), moments.end(), [forward](const Moment &a, const Moment &b) {
    return forward ? a.position < b.position : a.position > b.position;
  });

  auto open = [&](int index) {
    auto &list = m_windows[entity];
    list.push_back({layer, source.state->name, events[index].name, index, false, false});
    queueEvent(entity, events[index].name, Phase::Begin, layer, source.state->name);
  };
  auto close = [&](OpenWindow &record) {
    record.closing = true;
    queueEvent(entity, record.name, Phase::End, record.layer, record.state);
  };
  for (const Moment &moment : moments) {
    const AnimationGraph::Event &event = events[moment.event];
    if (moment.phase == Phase::Fire)
      queueEvent(entity, event.name, Phase::Fire, layer, source.state->name);
    else if (OpenWindow *record = openWindow(entity, layer, moment.event); moment.phase == Phase::End && record)
      close(*record);
    else if (moment.phase == Phase::Begin && !record)
      open(moment.event);
  }
  // Where the clock stands now decides the rest: inside a window is open, outside is closed (the moments above can
  // differ by a hair at an edge)
  for (size_t i = 0; i < events.size(); ++i) {
    if (!events[i].window)
      continue;
    const bool inside = after >= events[i].from && after < events[i].to;
    OpenWindow *record = openWindow(entity, layer, static_cast<int>(i));
    if (inside && !record)
      open(static_cast<int>(i));
    else if (!inside && record)
      close(*record);
  }
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
  closeAllWindows(entity);
  machine->state.clear();
  machine->params.clear();
  machine->manual = false;
  machine->layers.clear();
  runMachine(entity, *machine, 0.0f);
}

void AnimationSystem::enterState(Entity entity, AnimationStateMachine &machine, const AnimationGraph &graph, int index,
                                 float fadeSeconds) {
  const AnimationGraph::State &state = graph.base.states[index];
  closeWindows(entity, AnimationGraph::kBaseLayer);
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
  machine.clockMoved = false;
  machine.manual = false;
}

void AnimationSystem::enterLayerState(Entity entity, AnimationStateMachine &machine,
                                      const AnimationGraph::Layer &layer, int layerIndex, int index,
                                      float fadeSeconds) {
  AnimationStateMachine::LayerState &runtime = machine.layers[layerIndex];
  const AnimationGraph::State &state = layer.states[index];
  closeWindows(entity, layer.name);
  runtime.state = state.name;
  runtime.stateTime = 0.0f;
  runtime.clockMoved = false;
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

int AnimationSystem::runMachine(Entity entity, AnimationStateMachine &machine, float deltaTime,
                                std::shared_ptr<const AnimationGraph> *heldOut) {
  const std::shared_ptr<const AnimationGraph> held = graph(machine.path);
  if (!held)
    return -1;
  if (heldOut)
    *heldOut = held;
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
    // What a layer the file dropped had open is closed
    if (!m_windows.empty())
      for (const AnimationStateMachine::LayerState &old : machine.layers) {
        const bool kept = std::any_of(graph.layers.begin(), graph.layers.end(),
                                      [&](const AnimationGraph::Layer &layer) { return layer.name == old.name; });
        if (!kept)
          closeWindows(entity, old.name);
      }
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
      closeWindows(entity, layer.name);
      machine.layers[i] = AnimationStateMachine::LayerState();
      machine.layers[i].name = layer.name;
      enterLayerState(entity, machine, layer, static_cast<int>(i), layer.defaultState, 0.0f);
    }
  }

  // A window the file no longer has (it was edited while the game ran) closes
  if (!m_windows.empty())
    validateWindows(entity, machine, graph);

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
    return current;

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
  if (!machine.manual)
    if (Animator *animator = m_componentManager.tryGet<Animator>(entity))
      driveState(*animator, graph.base.states[current], machine, graph);
  return current;
}

const std::vector<glm::mat4> *AnimationSystem::getJointMatrices(Entity entity) const {
  auto it = m_joints.find(entity);
  if (it != m_joints.end())
    return &it->second;
  const Model *model = m_componentManager.tryGet<Model>(entity);
  const MeshAnimation *animation = model ? m_resourceSystem.getMesh(model->meshHandle).getAnimation() : nullptr;
  return animation ? &animation->restJointMatrices : nullptr;
}
