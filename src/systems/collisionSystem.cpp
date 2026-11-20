// AABB collision queries: world boxes from Collision + Transform, sort and sweep for the pairs.

#include "systems/collisionSystem.h"

#include "components/collision.h"
#include "components/transform.h"
#include <algorithm>
#include <cmath>

namespace {
bool boxesOverlap(const glm::vec3 &minA, const glm::vec3 &maxA, const glm::vec3 &minB, const glm::vec3 &maxB) {
  return maxA.x > minB.x && minA.x < maxB.x && maxA.y > minB.y && minA.y < maxB.y && maxA.z > minB.z &&
         minA.z < maxB.z;
}
} // namespace

const std::vector<CollisionEvent> &CollisionSystem::findCollisions(ComponentManager &componentManager) {
  m_boxes.clear();
  m_collisions.clear();
  // Layer names are only looked up when some pair does not collide; with the default matrix this costs nothing
  const bool useLayers = !m_layers.allCollide();
  componentManager.forEachComponent<Collision>([&](Entity entity, Collision &collision) {
    const auto *transform = componentManager.tryGet<Transform>(entity);
    const glm::vec3 offset = transform ? transform->position : glm::vec3(0.0f);
    m_boxes.push_back({entity, collision.min + offset, collision.max + offset, collision.isStatic,
                       useLayers ? m_layers.resolve(collision.layer) : 0});
  });

  // Sorted by their left edge, a box can only overlap the boxes after it that start before it ends
  std::sort(m_boxes.begin(), m_boxes.end(), [](const Box &a, const Box &b) { return a.min.x < b.min.x; });
  for (size_t i = 0; i < m_boxes.size(); ++i) {
    const Box &a = m_boxes[i];
    for (size_t j = i + 1; j < m_boxes.size() && m_boxes[j].min.x < a.max.x; ++j) {
      const Box &b = m_boxes[j];
      if ((a.isStatic && b.isStatic) || !boxesOverlap(a.min, a.max, b.min, b.max) ||
          (useLayers && !m_layers.collide(a.layer, b.layer)))
        continue;
      m_collisions.push_back({a.entity, b.entity});
    }
  }
  return m_collisions;
}

std::optional<RayHit> CollisionSystem::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                               ComponentManager &componentManager,
                                               const std::function<bool(Entity)> &accept, uint32_t layerMask) const {
  const float length = glm::length(direction);
  if (length <= 0.0f || maxDistance < 0.0f)
    return std::nullopt;
  const glm::vec3 dir = direction / length;

  std::optional<RayHit> nearest;
  componentManager.forEachComponent<Collision>([&](Entity entity, Collision &collision) {
    if (accept && !accept(entity))
      return;
    if (layerMask != CollisionLayers::kAllLayers && ((layerMask >> m_layers.resolve(collision.layer)) & 1u) == 0)
      return;
    const auto *transform = componentManager.tryGet<Transform>(entity);
    const glm::vec3 offset = transform ? transform->position : glm::vec3(0.0f);
    const glm::vec3 boxMin = collision.min + offset, boxMax = collision.max + offset;

    // Slabs: the ray is inside the box between the last entry and the first exit over the three axes
    float enter = 0.0f;
    float exit = nearest ? nearest->distance : maxDistance;
    glm::vec3 normal(0.0f);
    for (int axis = 0; axis < 3; ++axis) {
      if (std::abs(dir[axis]) < 1e-8f) {
        if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis])
          return; // parallel to this slab and outside it
        continue;
      }
      float t0 = (boxMin[axis] - origin[axis]) / dir[axis];
      float t1 = (boxMax[axis] - origin[axis]) / dir[axis];
      if (t0 > t1)
        std::swap(t0, t1);
      if (t0 > enter) {
        normal = glm::vec3(0.0f);
        normal[axis] = dir[axis] > 0.0f ? -1.0f : 1.0f; // the face the ray enters through
      }
      enter = std::max(enter, t0);
      exit = std::min(exit, t1);
      if (enter > exit)
        return;
    }
    nearest = RayHit{entity, enter, origin + dir * enter, normal};
  });
  return nearest;
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
  if (!m_layers.allCollide() && !m_layers.collide(collisionA->layer, collisionB->layer))
    return false;
  return boxesOverlap(collisionA->min + offsetA, collisionA->max + offsetA, collisionB->min + offsetB,
                      collisionB->max + offsetB);
}
