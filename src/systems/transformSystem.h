#pragma once
// Model matrices from Transforms, and the parent/child hierarchy (see components/parent.h).

#include "components/transform.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include <glm/gtc/quaternion.hpp>
#include <vector>

class TransformSystem : public BaseSystem {
public:
  TransformSystem() = default;
  // Lets setParent keep a child in the scene of its parent (see below)
  void setEntityManager(EntityManager &entityManager) { m_entityManager = &entityManager; }
  static glm::mat4 calculateModelMatrix(const Transform &transform);
  // The other way: position, rotation (in the same Y, X, Z order) and scale of a matrix. A parent scaled unevenly
  // and rotated can make a child's matrix skewed, which a Transform cannot hold; the closest one comes back.
  static Transform decompose(const glm::mat4 &matrix);
  // A rotation as a Transform's angles (degrees, the same Y, X, Z order). At a pitch of +-90 degrees the yaw and the
  // roll turn around the same axis (gimbal lock) and only their sum counts: the roll is 0 there, instead of a split
  // made of float noise (0.194 where 0 was meant). Rounded to 0.001 degrees, so 15 reads 15, not 14.99997.
  static glm::vec3 eulerDegrees(const glm::quat &rotation);

  // Places every child at its parent's transform combined with its local one, parents before their children. A
  // child whose Transform was moved since the last call keeps that place, and its local transform follows instead.
  void updateHierarchy(ComponentManager &componentManager);

  // Makes `child` follow `parent` (-1: follow nothing), keeping its place in the world. False, with a warning, when
  // `parent` is the child itself or one of its descendants, or either has no Transform. A child belongs to its
  // parent's scene (additive scenes): parenting under an entity of another scene moves the child and everything
  // below it there. Entities the game created (not in any scene) stay as they are.
  bool setParent(ComponentManager &componentManager, Entity child, Entity parent);
  // `child` relative to its parent from now on (a child is placed right away, not on the next update)
  void setLocalTransform(ComponentManager &componentManager, Entity child, const Transform &local);
  Entity getParent(ComponentManager &componentManager, Entity entity) const;
  std::vector<Entity> getChildren(ComponentManager &componentManager, Entity entity) const;
  // Children, their children and so on, parents before their children
  std::vector<Entity> getDescendants(ComponentManager &componentManager, Entity entity) const;

private:
  EntityManager *m_entityManager = nullptr;
};
