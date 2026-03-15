#pragma once
// Collision system: AABB detection and event dispatch.
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "components/collisionComponent.h"
#include "components/transformComponent.h"
#include "components/modelComponent.h"
#include <vector>
#include <functional>

struct CollisionEvent {
    Entity a;
    Entity b;
};

class CollisionSystem : public BaseSystem {
public:
    CollisionSystem() = default;
    ~CollisionSystem() = default;

    // Called every frame
    void update(ComponentManager &componentManager);

    // Optional: register callback for collision events
    void setCollisionCallback(std::function<void(const CollisionEvent &)> cb) { m_callback = cb; }

    // Check collision between two entities (both must have ModelComponent and CollisionComponent)
    bool checkEntitiesCollision(Entity a, Entity b, ComponentManager &componentManager);

private:
    std::function<void(const CollisionEvent &)> m_callback;
    bool checkAABB(const glm::vec3 &minA, const glm::vec3 &maxA, const glm::vec3 &minB, const glm::vec3 &maxB);
};
