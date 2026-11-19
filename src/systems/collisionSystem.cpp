// AABB collision queries: world boxes from Collision + Transform, sort and sweep for the pairs.

#include "systems/collisionSystem.h"

#include "components/collision.h"
#include "components/transform.h"
#include <algorithm>

namespace {
bool boxesOverlap(const glm::vec3 &minA, const glm::vec3 &maxA, const glm::vec3 &minB, const glm::vec3 &maxB) {
  return maxA.x > minB.x && minA.x < maxB.x && maxA.y > minB.y && minA.y < maxB.y && maxA.z > minB.z &&
         minA.z < maxB.z;
}
} // namespace

const std::vector<CollisionEvent> &CollisionSystem::findCollisions(ComponentManager &componentManager) {
  m_boxes.clear();
  m_collisions.clear();
  componentManager.forEachComponent<Collision>([&](Entity entity, Collision &collision) {
    const auto *transform = componentManager.tryGet<Transform>(entity);
    const glm::vec3 offset = transform ? transform->position : glm::vec3(0.0f);
    m_boxes.push_back({entity, collision.min + offset, collision.max + offset, collision.isStatic});
  });

  // Sorted by their left edge, a box can only overlap the boxes after it that start before it ends
  std::sort(m_boxes.begin(), m_boxes.end(), [](const Box &a, const Box &b) { return a.min.x < b.min.x; });
  for (size_t i = 0; i < m_boxes.size(); ++i) {
    const Box &a = m_boxes[i];
    for (size_t j = i + 1; j < m_boxes.size() && m_boxes[j].min.x < a.max.x; ++j) {
      const Box &b = m_boxes[j];
      if ((a.isStatic && b.isStatic) || !boxesOverlap(a.min, a.max, b.min, b.max))
        continue;
      m_collisions.push_back({a.entity, b.entity});
    }
  }
  return m_collisions;
}

bool CollisionSystem::overlaps(Entity a, Entity b, ComponentManager &componentManager) const {
  const auto *collisionA = componentManager.tryGet<Collision>(a);
  const auto *collisionB = componentManager.tryGet<Collision>(b);
  if (!collisionA || !collisionB)
    return false;
  const auto *transformA = componentManager.tryGet<Transform>(a);
  const auto *transformB = componentManager.tryGet<Transform>(b);
  const glm::vec3 offsetA = transformA ? transformA->position : glm::vec3(0.0f);
  const glm::vec3 offsetB = transformB ? transformB->position : glm::vec3(0.0f);
  return boxesOverlap(collisionA->min + offsetA, collisionA->max + offsetA, collisionB->min + offsetB,
                      collisionB->max + offsetB);
}
