#pragma once
// AABB collision queries between entities with Collision components.
//
// A Collision box is min/max around the entity's Transform position (not rotated or scaled). Queries look at the
// positions as they are when called, so a game can move things and then ask what touches what in the same frame.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

struct CollisionEvent {
  Entity a;
  Entity b;
};

struct RayHit {
  Entity entity;
  float distance; // along the ray, from its origin
  glm::vec3 point;
  glm::vec3 normal{0.0f}; // of the surface hit, facing the ray (zero when the ray starts inside)
};

class CollisionSystem : public BaseSystem {
public:
  // Every pair of entities whose boxes overlap now, each pair once (two static boxes never count). Sort and sweep
  // along x: about n log n plus the pairs found, rather than testing every pair.
  const std::vector<CollisionEvent> &findCollisions(ComponentManager &componentManager);
  // Whether these two entities' boxes overlap now (false if either has no Collision)
  bool overlaps(Entity a, Entity b, ComponentManager &componentManager) const;
  // The nearest box the ray enters within maxDistance (a ray starting inside a box hits it at distance 0).
  // `accept` skips entities it returns false for. With maxDistance the length of a step, this is a swept test:
  // nothing fast slips through a thin box between two frames.
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                ComponentManager &componentManager,
                                const std::function<bool(Entity)> &accept = nullptr) const;

private:
  struct Box {
    Entity entity;
    glm::vec3 min, max;
    bool isStatic;
  };
  std::vector<Box> m_boxes;
  std::vector<CollisionEvent> m_collisions;
};
