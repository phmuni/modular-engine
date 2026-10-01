#pragma once
// AABB collision system for detecting collisions between entities with Collision components.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>

struct CollisionEvent {
  Entity a;
  Entity b;
};

class CollisionSystem : public BaseSystem {
public:
  CollisionSystem() = default;
  ~CollisionSystem() = default;

  void update(ComponentManager &componentManager);

  void setCollisionCallback(std::function<void(const CollisionEvent &)> cb) { m_callback = cb; }

  bool checkEntitiesCollision(Entity a, Entity b, ComponentManager &componentManager);

private:
  std::function<void(const CollisionEvent &)> m_callback;
  bool checkAABB(const glm::vec3 &minA, const glm::vec3 &maxA, const glm::vec3 &minB, const glm::vec3 &maxB);
};
