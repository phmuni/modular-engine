#pragma once
// Skeletal animation: poses the skeleton of every animated model with an Animator from its clip (crossfading between
// two while switching) and keeps the joint matrices the renderer skins with.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ResourceSystem;
struct MeshAnimation;
struct AnimationClip;
struct NodePose;
struct Animator;
struct AnimationGraph;
struct AnimationStateMachine;

class AnimationSystem : public BaseSystem {
public:
  AnimationSystem(ComponentManager &componentManager, ResourceSystem &resourceSystem)
      : m_componentManager(componentManager), m_resourceSystem(resourceSystem) {}

  // Runs every AnimationStateMachine (parameters read, transitions fired), then advances every Animator by deltaTime
  // (0 while paused: it holds its pose, and the machines wait)
  void update(float deltaTime);

  // A state machine's definition, read from the file once and again after it changes on disk; null for a file that is
  // missing or invalid (logged once, with the reason). The pointer stays good while the caller holds it.
  std::shared_ptr<const AnimationGraph> graph(const std::string &path);
  // Puts the entity's state machine in its default state if it has not started (the Animator is added if needed)
  void startMachine(Entity entity);
  // Makes the machine start over from the default state with every parameter at the file's initial value
  void restartMachine(Entity entity);

  // The state changes since the last call, oldest first: the entity, the state it left and the one it entered
  struct StateChange {
    Entity entity;
    std::string from, to;
  };
  std::vector<StateChange> takeStateChanges();

  // Switches the animator to `clip`, fading from what it plays over `fadeSeconds` (none when 0); the time starts over.
  // `animation` is the model's, for names and lengths (may be null).
  static void crossfade(Animator &animator, const MeshAnimation *animation, const std::string &clip,
                        float fadeSeconds, bool loop);

  // The joint matrices to skin the entity's model with this frame: its Animator's pose, or the model's rest pose
  // when it has none; null for a static model
  const std::vector<glm::mat4> *getJointMatrices(Entity entity) const;

  // The pose of `clip` at `time` (seconds, wrapped when looping) over the rest pose
  static void sample(const MeshAnimation &animation, const AnimationClip &clip, float time, std::vector<NodePose> &pose);

private:
  ComponentManager &m_componentManager;
  ResourceSystem &m_resourceSystem;
  struct CachedGraph {
    std::shared_ptr<const AnimationGraph> graph; // null when the file was not usable
    std::filesystem::file_time_type modified;
    bool exists = false;
  };
  std::unordered_map<std::string, CachedGraph> m_graphs;
  int m_graphCheck = 0; // frames since the definition files were last looked at
  std::vector<StateChange> m_changes;

  const MeshAnimation *animationOf(Entity entity) const;
  void enterState(Entity entity, AnimationStateMachine &machine, const AnimationGraph &graph, int state,
                  float fadeSeconds);
  void runMachine(Entity entity, AnimationStateMachine &machine, float deltaTime);
  void reloadChangedGraphs();

  std::unordered_map<Entity, std::vector<glm::mat4>> m_joints;
  std::vector<glm::mat4> m_globals; // scratch
};
