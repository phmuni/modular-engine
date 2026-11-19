#pragma once
// Physics with Jolt: RigidBody and CharacterController components become Jolt bodies and characters.
//
// Each update the system creates bodies for new components and removes those of destroyed entities, hands Transforms
// that game code or the editor moved over to the simulation, steps it in fixed 1/60 s steps, and writes the result
// back into the Transforms of dynamic bodies and characters. Changing a RigidBody's settings (or the entity's scale)
// rebuilds its body. Jolt itself stays inside physicsSystem.cpp.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include "systems/collisionSystem.h"
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <vector>

class ResourceSystem;
class TransformSystem;

struct PhysicsContact {
  Entity a;
  Entity b;
  glm::vec3 point;  // world space
  glm::vec3 normal; // from a toward b
};

class PhysicsSystem : public BaseSystem {
public:
  PhysicsSystem(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                TransformSystem &transformSystem);
  ~PhysicsSystem();

  // Syncs bodies with the components and advances the simulation by deltaTime (0, while paused: no step)
  void update(float deltaTime);
  // Removes every body and character; they are created again from the components on the next update. For when the
  // entities were replaced wholesale (the editor's Stop).
  void reset();

  glm::vec3 getGravity() const;
  void setGravity(const glm::vec3 &gravity);

  // Dynamic bodies (no effect on others, or before the body exists: at the latest one update after the component)
  glm::vec3 getLinearVelocity(Entity entity) const;
  void setLinearVelocity(Entity entity, const glm::vec3 &velocity);
  glm::vec3 getAngularVelocity(Entity entity) const;
  void setAngularVelocity(Entity entity, const glm::vec3 &velocity);
  void addImpulse(Entity entity, const glm::vec3 &impulse); // kg * units/s, at the center of mass
  void addForce(Entity entity, const glm::vec3 &force);     // for this step

  // The nearest body along a ray (characters are not bodies); `accept` skips entities it returns false for
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                const std::function<bool(Entity)> &accept = nullptr) const;

  // Bodies that started touching during the last update (sensors included), each pair once
  const std::vector<PhysicsContact> &getContacts() const;

  size_t getBodyCount() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
