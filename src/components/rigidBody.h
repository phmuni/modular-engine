#pragma once
// A physics body (Jolt): the entity falls, collides, slides and bounces. See PhysicsSystem.
//
// Static bodies never move (floors, walls, level geometry: Mesh fits them exactly); dynamic ones are moved by the
// simulation, which writes their Transform; kinematic ones follow their Transform (moved by game code or animation)
// and push dynamic ones out of the way. The shape is built in the entity's own space and scaled by its Transform.

#include <glm/glm.hpp>
#include <string>

enum class BodyType { Static, Dynamic, Kinematic };
enum class BodyShape {
  Box,
  Sphere,
  Capsule,    // upright: round at both ends
  ConvexHull, // wraps the model's vertices; for dynamic models that are not boxes or balls
  Mesh,       // the model's triangles exactly; static and kinematic bodies only (a dynamic one uses ConvexHull)
};

struct RigidBody {
  BodyType type = BodyType::Dynamic;
  BodyShape shape = BodyShape::Box;
  // Box: full size; Sphere: diameter (x); Capsule: diameter (x) and total height (y). All zero: from the model's
  // bounds, which also places the shape on the model when its origin is not its middle.
  glm::vec3 size{0.0f};
  float mass = 0.0f; // kg; 0: from the shape's volume (1000 kg per cubic unit, like water)
  float friction = 0.5f;
  float restitution = 0.0f; // bounciness, 0 to 1
  float linearDamping = 0.05f;
  float angularDamping = 0.05f;
  float gravityScale = 1.0f;
  bool lockRotation = false; // only moves, never turns (dynamic)
  bool sensor = false;       // reports contacts but lets things pass through
  // The collision layer it is on, by name (see CollisionLayers): which other layers it hits. Changing it takes effect
  // at once, without rebuilding the body.
  std::string layer = "default";

  // Equal in everything that needs the body built again: the layer is applied to the live body instead
  friend bool operator==(const RigidBody &a, const RigidBody &b) {
    return a.type == b.type && a.shape == b.shape && a.size == b.size && a.mass == b.mass &&
           a.friction == b.friction && a.restitution == b.restitution && a.linearDamping == b.linearDamping &&
           a.angularDamping == b.angularDamping && a.gravityScale == b.gravityScale &&
           a.lockRotation == b.lockRotation && a.sensor == b.sensor;
  }
  friend bool operator!=(const RigidBody &a, const RigidBody &b) { return !(a == b); }
};
