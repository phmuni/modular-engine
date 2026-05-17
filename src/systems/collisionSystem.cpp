
// Collision system implementation for handling collision detection between entities using AABBs.

#include "systems/collisionSystem.h"
#include "components/collision.h"
#include "components/model.h"
#include "components/transform.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "systems/collisionSystem.h"
#include <glm/glm.hpp>

void CollisionSystem::update(ComponentManager &componentManager) {
  std::vector<CollisionEvent> collisions;

  std::vector<std::pair<Entity, Collision *>> colliders;
  componentManager.forEachComponent<Collision>(
      [&](Entity entity, Collision &col) { colliders.emplace_back(entity, &col); });

  // Brute-force pairwise check (optimize later)
  for (size_t i = 0; i < colliders.size(); ++i) {
    Entity a = colliders[i].first;
    Collision *colA = colliders[i].second;
    auto *transA = componentManager.getOrNil<Transform>(a);
    glm::vec3 minA = colA->min;
    glm::vec3 maxA = colA->max;
    if (transA) {
      minA += transA->position;
      maxA += transA->position;
    }
    for (size_t j = i + 1; j < colliders.size(); ++j) {
      Entity b = colliders[j].first;
      Collision *colB = colliders[j].second;
      auto *transB = componentManager.getOrNil<Transform>(b);
      glm::vec3 minB = colB->min;
      glm::vec3 maxB = colB->max;
      if (transB) {
        minB += transB->position;
        maxB += transB->position;
      }
      if (checkAABB(minA, maxA, minB, maxB)) {
        collisions.push_back({a, b});
      }
    }
  }
  // Dispatch collision events
  if (m_callback) {
    for (const auto &ev : collisions)
      m_callback(ev);
  }
}

bool CollisionSystem::checkAABB(const glm::vec3 &minA, const glm::vec3 &maxA, const glm::vec3 &minB,
                                const glm::vec3 &maxB) {
  return (maxA.x > minB.x && minA.x < maxB.x && maxA.y > minB.y && minA.y < maxB.y && maxA.z > minB.z &&
          minA.z < maxB.z);
}

bool CollisionSystem::checkEntitiesCollision(Entity a, Entity b, ComponentManager &componentManager) {
  // Both entities must have Model and Collision to be considered for collision
  if (!componentManager.containsComponent<Model>(a) || !componentManager.containsComponent<Model>(b))
    return false;
  auto *colA = componentManager.getOrNil<Collision>(a);
  auto *colB = componentManager.getOrNil<Collision>(b);
  if (!colA || !colB)
    return false;
  auto *transA = componentManager.getOrNil<Transform>(a);
  auto *transB = componentManager.getOrNil<Transform>(b);
  glm::vec3 minA = colA->min;
  glm::vec3 maxA = colA->max;
  glm::vec3 minB = colB->min;
  glm::vec3 maxB = colB->max;
  if (transA) {
    minA += transA->position;
    maxA += transA->position;
  }
  if (transB) {
    minB += transB->position;
    maxB += transB->position;
  }
  return checkAABB(minA, maxA, minB, maxB);
}
