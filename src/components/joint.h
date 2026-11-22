#pragma once
// A physics joint (a Jolt constraint) between this entity's body and another's, or the world: a door on its hinge, a
// wall spun by a motor, an elevator on a rail, a pendulum, two parts welded together. See PhysicsSystem.
//
// Put it on an entity with a RigidBody (normally a dynamic one: that is the body the joint moves). `target` names the
// entity it is joined to, which needs a RigidBody of any type too; empty joins it to the world (a fixed point). A body
// that a joint holds still collides with everything else, so a wall turned by a hinge motor is stopped by a pillar in
// its way instead of passing through it. One joint per entity: for a chain, give each link a joint to the one before.
//
// Angles are in degrees and everything is relative to how the two bodies stand when the joint is built (the scene
// loads, the component is added or changed): a hinge's angle and a slider's position are 0 there. So limits are
// [-180, 0] to [0, 180] degrees for a hinge and min <= 0 <= max for a slider, around that starting pose.
//
//   Hinge    turns about `axis` through `anchor`, like a door or a wheel. Limits and motor are in degrees.
//   Slider   moves along `axis` only, like a drawer or an elevator. Limits and motor are in world units.
//   Fixed    welds the two bodies in the pose they have when it is built.
//   Distance keeps `anchor` (on this entity) and `targetAnchor` (on the target) between minDistance and maxDistance
//            apart: a rod (both equal), a rope (min 0) or, with the spring, a bungee.

#include <glm/glm.hpp>
#include <string>

enum class JointType { Hinge, Fixed, Distance, Slider };

// What drives a hinge or slider: nothing (it moves freely), a speed to keep up, or a pose to go to
enum class JointMotor { Off, Velocity, Position };

struct Joint {
  JointType type = JointType::Hinge;
  // The Name of the entity it is joined to (the lowest entity number when several share it); empty: the world. It is
  // looked up again when the name changes or the entity is destroyed. Until the target exists and has a RigidBody
  // nothing is built (a warning is logged once).
  std::string target;
  // The pivot (hinge), or where the joint is attached (distance), in this entity's own space, scaled by its Transform
  // like a body's shape. The target's side is at the same world point, except for a distance joint (see targetAnchor).
  glm::vec3 anchor{0.0f};
  // The other end of a distance joint: in the target's own space (scaled like `anchor`), or a point in the world when
  // `target` is empty. Not used by the other types.
  glm::vec3 targetAnchor{0.0f};
  // The hinge's turning axis, or the slider's direction, in this entity's own space; not used by fixed and distance
  glm::vec3 axis{0.0f, 1.0f, 0.0f};

  // Hinge and slider: stop at min and max (degrees / units, relative to the pose the joint was built in)
  bool limits = false;
  float min = -90.0f;
  float max = 90.0f;
  // Distance: how far apart the two points stay. Both 0: the distance they are at when the joint is built.
  float minDistance = 0.0f;
  float maxDistance = 0.0f;

  // The motor of a hinge or slider. Velocity: keeps `motorSpeed` (deg/s, units/s) up. Position: goes to `motorTarget`
  // (deg, units) and holds it, as a spring (see below). Both are limited by maxMotorForce, so a motor pushing against
  // something that does not give (a pillar, a closed gate) STALLS at that force instead of crushing it or driving the
  // bodies through each other.
  JointMotor motor = JointMotor::Off;
  float motorSpeed = 0.0f;
  float motorTarget = 0.0f;
  // N*m for a hinge, N for a slider. Finite on purpose (an unlimited motor is what makes a joint explode when it is
  // blocked): 100000 is ten times the weight of a tonne, enough to turn a heavy door or lift an elevator car at the
  // default density (1000 kg per cubic unit) while a body in the way, which the solver resists far harder with its
  // contacts, still wins. Lower it for a weak motor, raise it for a very heavy mechanism. It also caps what a position
  // motor's spring pulls with: a stiff spring on something heavy then swings past its target before it settles, so a
  // heavy mechanism that must hold a pose wants a higher limit (or a lower frequency).
  float maxMotorForce = 100000.0f;

  // The spring. For a hinge or slider it is the position motor's: it pulls toward motorTarget with this stiffness
  // (frequency, Hz) and damping (0: it oscillates, 1: it settles without overshoot). A door that swings back shut is
  // motor Position, target 0 and a low frequency (1 to 3 Hz); a very low one is a weak spring that gives way to pushes.
  // 0 here means a stiff 10 Hz, since a position motor always needs some spring. For a distance joint they soften
  // the min/max limits instead (a rope that stretches, a bungee); 0 keeps them hard. Not used by a velocity motor.
  float springFrequency = 0.0f;
  float springDamping = 1.0f;

  // Whether the two joined bodies collide with each other (like Unity's "Enable Collision"). Off by default: they
  // overlap at the joint, and a door and its frame would fight there.
  bool collideConnected = false;

  // Equal in everything that needs the constraint built again. What a motor and the spring do is applied to the live
  // constraint instead (motor, motorSpeed, motorTarget, maxMotorForce, springFrequency, springDamping), so a script
  // can steer a motor every frame.
  friend bool operator==(const Joint &a, const Joint &b) {
    return a.type == b.type && a.target == b.target && a.anchor == b.anchor && a.targetAnchor == b.targetAnchor &&
           a.axis == b.axis && a.limits == b.limits && a.min == b.min && a.max == b.max &&
           a.minDistance == b.minDistance && a.maxDistance == b.maxDistance &&
           a.collideConnected == b.collideConnected;
  }
  friend bool operator!=(const Joint &a, const Joint &b) { return !(a == b); }
  // Whether the live fields are the same too
  bool sameMotor(const Joint &o) const {
    return motor == o.motor && motorSpeed == o.motorSpeed && motorTarget == o.motorTarget &&
           maxMotorForce == o.maxMotorForce && springFrequency == o.springFrequency &&
           springDamping == o.springDamping;
  }
};
