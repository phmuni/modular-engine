// Collision system: AABB detection and event dispatch.

#include "systems/collisionSystem.h"
#include "systems/collisionSystem.h"
#include "components/collision.h"
#include "components/model.h"
#include "components/transform.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/componentManager.h"
#include <glm/glm.hpp>

void CollisionSystem::update(ComponentManager &componentManager) {
    std::vector<CollisionEvent> collisions;
    // Iterate all entities with Collision
    std::vector<std::pair<Entity, Collision*>> colliders;
    componentManager.each<Collision>([&](Entity entity, Collision &col) {
        colliders.emplace_back(entity, &col);
    });

    // Brute-force pairwise check (can optimize later)
    for (size_t i = 0; i < colliders.size(); ++i) {
        Entity a = colliders[i].first;
        Collision *colA = colliders[i].second;
        auto *transA = componentManager.tryGet<Transform>(a);
        glm::vec3 minA = colA->min;
        glm::vec3 maxA = colA->max;
        if (transA) {
            minA += transA->position;
            maxA += transA->position;
        }
        for (size_t j = i + 1; j < colliders.size(); ++j) {
            Entity b = colliders[j].first;
            Collision *colB = colliders[j].second;
            auto *transB = componentManager.tryGet<Transform>(b);
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
        for (const auto &ev : collisions) m_callback(ev);
    }
}

bool CollisionSystem::checkAABB(const glm::vec3 &minA, const glm::vec3 &maxA, const glm::vec3 &minB, const glm::vec3 &maxB) {
    return (maxA.x > minB.x && minA.x < maxB.x &&
            maxA.y > minB.y && minA.y < maxB.y &&
            maxA.z > minB.z && minA.z < maxB.z);
}

bool CollisionSystem::checkEntitiesCollision(Entity a, Entity b, ComponentManager &componentManager) {
    // Both entities must have Model and Collision to be considered for collision
    if (!componentManager.has<Model>(a) || !componentManager.has<Model>(b))
        return false;
    auto *colA = componentManager.tryGet<Collision>(a);
    auto *colB = componentManager.tryGet<Collision>(b);
    if (!colA || !colB)
        return false;
    auto *transA = componentManager.tryGet<Transform>(a);
    auto *transB = componentManager.tryGet<Transform>(b);
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
