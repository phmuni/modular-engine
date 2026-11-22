#pragma once
// Physics with Jolt: RigidBody and CharacterController components become Jolt bodies and characters.
//
// Each update the system creates bodies for new components and removes those of destroyed entities, hands Transforms
// that game code or the editor moved over to the simulation, steps it in fixed 1/60 s steps, and writes the result
// back into the Transforms of dynamic bodies and characters. Changing a RigidBody's settings (or the entity's scale)
// rebuilds its body; changing its layer (or the layer matrix) does not, the live body is moved to the new layer.
// Which bodies and characters collide follows CollisionLayers. Jolt itself stays inside physicsSystem.cpp.

#include "foundation/core/collisionLayers.h"
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

// Two bodies that stopped touching
struct PhysicsContactEnd {
  Entity a;
  Entity b;
};

class PhysicsSystem : public BaseSystem {
public:
  PhysicsSystem(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                TransformSystem &transformSystem, const CollisionLayers &layers);
  ~PhysicsSystem();

  // Syncs bodies with the components and advances the simulation by deltaTime (0, while paused: no step)
  void update(float deltaTime);
  // Creates the bodies of components added since the last update right now, so queries see them (for game code that
  // builds something from the physics world in its first frame, like the navigation map)
  void syncBodies();
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
  void addTorque(Entity entity, const glm::vec3 &torque);   // N * m, for this step
  void addAngularImpulse(Entity entity, const glm::vec3 &impulse); // N * m * s, at once
  void addImpulseAt(Entity entity, const glm::vec3 &impulse, const glm::vec3 &worldPoint); // off the center: spins too
  void addForceAt(Entity entity, const glm::vec3 &force, const glm::vec3 &worldPoint); // for this step, off the center

  // The nearest body along a ray; `accept` skips entities it returns false for, and only bodies on the layers in
  // `layerMask` (bit n: layer n, see CollisionLayers::maskOf) are hit. Characters are not hit: see QueryOptions.
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                const std::function<bool(Entity)> &accept = nullptr,
                                uint32_t layerMask = CollisionLayers::kAllLayers) const;
  // The same with options: layers, entities to ignore, and `characters` to let the ray hit character controllers
  // (their kinematic inner body, reported as the character's entity), which it passes through by default
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                const QueryOptions &options) const;

  // The entities whose bodies overlap a sphere or a box (`rotation` in degrees, as a Transform's), each once; sensors
  // count. The options are the raycast's: layers, entities to ignore, and characters (left out by default).
  std::vector<Entity> overlapSphere(const glm::vec3 &center, float radius, const QueryOptions &options = {}) const;
  std::vector<Entity> overlapBox(const glm::vec3 &center, const glm::vec3 &halfExtents,
                                 const glm::vec3 &rotation = glm::vec3(0.0f), const QueryOptions &options = {}) const;

  // Whether an upright capsule standing on `base` (its lowest point), `height` tall in all and `radius` wide,
  // overlaps a static or kinematic body (dynamic bodies and sensors are not obstacles: they move or do not block)
  bool isCapsuleBlocked(const glm::vec3 &base, float height, float radius) const;
  // The box around every static body that is not a sensor; false when there are none
  bool getStaticBounds(glm::vec3 &min, glm::vec3 &max) const;

  // Bodies that started touching during the last update (sensors included), each pair once however many parts of their
  // shapes touch
  const std::vector<PhysicsContact> &getContacts() const;
  // Pairs that stopped touching during the last update (sensors included), each pair once, when their last part let go.
  // A pair that touched and separated within one update is in both lists. A body destroyed (or rebuilt, by changing its
  // RigidBody or scale) while touching ends its contacts without an entry here: only entities that still have their
  // body are listed.
  const std::vector<PhysicsContactEnd> &getContactEnds() const;

  size_t getBodyCount() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
