#pragma once
// A character that walks on what physics bodies make up: an upright capsule that stays on the ground, climbs slopes
// up to maxSlope and steps up stairs up to stepHeight, is pushed by nothing and pushes dynamic bodies. Its Transform
// is the capsule's middle. Each frame the game sets `move` (and `jump` to jump); PhysicsSystem moves it and fills in
// `onGround` and `velocity`.

#include <glm/glm.hpp>
#include <string>

struct CharacterController {
  float radius = 0.35f;
  float height = 1.8f;    // total, both round ends included
  float maxSlope = 50.0f; // degrees
  float stepHeight = 0.35f;
  float gravityScale = 1.0f;
  // The collision layer it is on, by name (see CollisionLayers): it walks through what its layer does not collide with
  std::string layer = "default";

  // In: the horizontal velocity it wants this frame (world units per second; y is ignored), and a jump speed
  // (used, then reset to 0, when it stands on the ground)
  glm::vec3 move{0.0f};
  float jump = 0.0f;

  // Out, after each update
  bool onGround = false;
  glm::vec3 velocity{0.0f};
};
