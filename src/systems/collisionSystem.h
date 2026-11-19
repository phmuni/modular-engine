#pragma once
// AABB collision queries between entities with Collision components.
//
// A Collision box is min/max around the entity's Transform position (not rotated or scaled). Queries look at the
// positions as they are when called, so a game can move things and then ask what touches what in the same frame.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>
#include <vector>

struct CollisionEvent {
  Entity a;
  Entity b;
};

class CollisionSystem : public BaseSystem {
public:
  // Every pair of entities whose boxes overlap now, each pair once (two static boxes never count). Sort and sweep
  // along x: about n log n plus the pairs found, rather than testing every pair.
  const std::vector<CollisionEvent> &findCollisions(ComponentManager &componentManager);
  // Whether these two entities' boxes overlap now (false if either has no Collision)
  bool overlaps(Entity a, Entity b, ComponentManager &componentManager) const;

private:
  struct Box {
    Entity entity;
    glm::vec3 min, max;
    bool isStatic;
  };
  std::vector<Box> m_boxes;
  std::vector<CollisionEvent> m_collisions;
};
