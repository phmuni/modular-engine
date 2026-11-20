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

  // Joints of entities that are gone, or no longer animated, are dropped below
  for (auto &[entity, joints] : m_joints)
    joints.clear();

  std::vector<NodePose> pose, otherPose, fadingPose;
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
    const AnimationClip *blendClip = animator.blendClip.empty() ? nullptr : findClip(*animation, animator.blendClip);

    bool finished = false;
    if (blendClip) {
      // Two clips in step: `time` is the cycle's phase, which moves at the pace of the mix of their lengths
      const float weight = std::clamp(animator.blendWeight, 0.0f, 1.0f);
      const float duration = clip->duration + (blendClip->duration - clip->duration) * weight;
      if (animator.playing) {
        const float step = duration > 0.0f ? deltaTime * animator.speed / duration : 0.0f;
        animator.time = advance(animator.time, step, 1.0f, animator.loop, finished);
        if (finished)
          animator.playing = false;
      }
      sample(*animation, *clip, animator.time * clip->duration, pose);
      sample(*animation, *blendClip, animator.time * blendClip->duration, otherPose);
      blendPoses(otherPose, pose, weight);
      pose.swap(otherPose);
    } else {
      if (animator.playing) {
        animator.time = advance(animator.time, deltaTime * animator.speed, clip->duration, animator.loop, finished);
        if (finished)
          animator.playing = false;
      }
      sample(*animation, *clip, animator.time, pose);
    }

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

void AnimationSystem::crossfade(Animator &animator, const MeshAnimation *animation, const std::string &clip,
                                float fadeSeconds, bool loop) {
  animator.loop = loop;
  animator.playing = true;
  if (fadeSeconds > 0.0f) {
    // What is on screen now is what it fades out; of a blend, the clip it leans to
    std::string from = animator.clip;
    float fromTime = animator.time;
    if (!animator.blendClip.empty()) {
      from = animator.blendWeight >= 0.5f ? animator.blendClip : animator.clip;
      const int index = animation ? animation->findClip(from) : -1;
      fromTime = index >= 0 ? animator.time * animation->clips[index].duration : 0.0f;
    } else if (from.empty() && animation && !animation->clips.empty()) {
      from = animation->clips[0].name;
    }
    animator.fadeFrom = from;
    animator.fadeFromTime = fromTime;
    animator.fadeElapsed = 0.0f;
    animator.fadeDuration = fadeSeconds;
  } else {
    animator.fadeFrom.clear();
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
  runMachine(entity, *machine, 0.0f);
}

void AnimationSystem::enterState(Entity entity, AnimationStateMachine &machine, const AnimationGraph &graph, int index,
                                 float fadeSeconds) {
  const AnimationGraph::State &state = graph.states[index];
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
  crossfade(*animator, animation, clip, fadeSeconds, state.loop);
  animator->speed = state.speed;
  machine.state = state.name;
  machine.stateTime = 0.0f;
  machine.manual = false;
}

void AnimationSystem::runMachine(Entity entity, AnimationStateMachine &machine, float deltaTime) {
  const std::shared_ptr<const AnimationGraph> held = graph(machine.path);
  if (!held)
    return;
  const AnimationGraph &graph = *held;

  int current = graph.findState(machine.state);
  if (current < 0) {
    // Starting (or the file changed and lost the state): the parameters it declares, then the default state
    for (auto it = machine.params.begin(); it != machine.params.end();)
      it = graph.params.count(it->first) ? std::next(it) : machine.params.erase(it);
    for (const auto &[name, param] : graph.params)
      machine.params.emplace(name, param.initial);
    current = graph.defaultState;
    enterState(entity, machine, graph, current, 0.0f);
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

  const AnimationGraph::State &state = graph.states[current];
  const MeshAnimation *animation = animationOf(entity);
  machine.stateTime += deltaTime * std::fabs(state.speed);

  // How long the state's clip is now (a blend's is the mix of its two), to measure exit times by
  float duration = 0.0f;
  const AnimationGraph::BlendPoint *first = nullptr, *second = nullptr;
  float weight = 0.0f;
  if (!state.blend.empty())
    blendPair(state, paramValue(machine, graph, state.blendParam), first, second, weight);
  if (animation) {
    const int a = animation->findClip(first ? first->clip : state.clip);
    const int b = second ? animation->findClip(second->clip) : a;
    if (a >= 0 && b >= 0)
      duration = animation->clips[a].duration + (animation->clips[b].duration - animation->clips[a].duration) * weight;
  }
  // Without a clip to measure, a wait for the end of it is over
  const float played = duration > 0.0f ? machine.stateTime / duration : 1.0e9f;

  for (const AnimationGraph::Transition &transition : graph.transitions) {
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

    // A trigger is used up by the transition that waited for it
    for (const AnimationGraph::Condition &condition : transition.when)
      if (condition.op == AnimationGraph::Op::Triggered)
        machine.params[condition.param] = 0.0f;
    const std::string from = machine.state;
    enterState(entity, machine, graph, transition.to, transition.fade);
    m_changes.push_back({entity, from, machine.state});
    current = transition.to;
    break;
  }

  // Drive the Animator with the state, unless a manual play took it over
  if (machine.manual)
    return;
  Animator *animator = m_componentManager.tryGet<Animator>(entity);
  if (!animator)
    return;
  const AnimationGraph::State &now = graph.states[current];
  animator->speed = now.speed;
  animator->loop = now.loop;
  if (!now.blend.empty()) {
    blendPair(now, paramValue(machine, graph, now.blendParam), first, second, weight);
    animator->clip = first->clip;
    animator->blendClip = second->clip;
    animator->blendWeight = weight;
  }
}

const std::vector<glm::mat4> *AnimationSystem::getJointMatrices(Entity entity) const {
  auto it = m_joints.find(entity);
  if (it != m_joints.end())
    return &it->second;
  const Model *model = m_componentManager.tryGet<Model>(entity);
  const MeshAnimation *animation = model ? m_resourceSystem.getMesh(model->meshHandle).getAnimation() : nullptr;
  return animation ? &animation->restJointMatrices : nullptr;
}
