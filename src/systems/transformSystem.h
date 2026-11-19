#pragma once
// Model matrices from Transforms, and the parent/child hierarchy (see components/parent.h).

#include "components/transform.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <vector>

class TransformSystem : public BaseSystem {
public:
  TransformSystem() = default;
  glm::mat4 calculateModelMatrix(const Transform &transform);
  // The other way: position, rotation (in the same Y, X, Z order) and scale of a matrix. A parent scaled unevenly
  // and rotated can make a child's matrix skewed, which a Transform cannot hold; the closest one comes back.
  static Transform decompose(const glm::mat4 &matrix);

  // Places every child at its parent's transform combined with its local one, parents before their children. A
  // child whose Transform was moved since the last call keeps that place, and its local transform follows instead.
  void updateHierarchy(ComponentManager &componentManager);

  // Makes `child` follow `parent` (-1: follow nothing), keeping its place in the world. False, with a warning, when
  // `parent` is the child itself or one of its descendants, or either has no Transform.
  bool setParent(ComponentManager &componentManager, Entity child, Entity parent);
  // `child` relative to its parent from now on (a child is placed right away, not on the next update)
  void setLocalTransform(ComponentManager &componentManager, Entity child, const Transform &local);
  Entity getParent(ComponentManager &componentManager, Entity entity) const;
  std::vector<Entity> getChildren(ComponentManager &componentManager, Entity entity) const;
  // Children, their children and so on, parents before their children
  std::vector<Entity> getDescendants(ComponentManager &componentManager, Entity entity) const;
};
