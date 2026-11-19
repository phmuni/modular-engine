#pragma once
// Skeletal animation: poses the skeleton of every animated model with an Animator from its clip (crossfading between
// two while switching) and keeps the joint matrices the renderer skins with.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class ResourceSystem;
struct MeshAnimation;
struct AnimationClip;
struct NodePose;

class AnimationSystem : public BaseSystem {
public:
  AnimationSystem(ComponentManager &componentManager, ResourceSystem &resourceSystem)
      : m_componentManager(componentManager), m_resourceSystem(resourceSystem) {}

  // Advances every Animator by deltaTime (0 while paused: it holds its pose)
  void update(float deltaTime);

  // The joint matrices to skin the entity's model with this frame: its Animator's pose, or the model's rest pose
  // when it has none; null for a static model
  const std::vector<glm::mat4> *getJointMatrices(Entity entity) const;

  // The pose of `clip` at `time` (seconds, wrapped when looping) over the rest pose
  static void sample(const MeshAnimation &animation, const AnimationClip &clip, float time, std::vector<NodePose> &pose);

private:
  ComponentManager &m_componentManager;
  ResourceSystem &m_resourceSystem;
  std::unordered_map<Entity, std::vector<glm::mat4>> m_joints;
  std::vector<glm::mat4> m_globals; // scratch
};
