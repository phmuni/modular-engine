// Rotations as a Transform's angles: steady where yaw and roll meet (a pitch of 90 degrees), and without float
// round-off, through TransformSystem::eulerDegrees and the hierarchy and physics paths that use it.

#include "foundation/core/engine.h"
#include "systems/transformSystem.h"
#include "testing.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cmath>

namespace {
glm::quat fromDegrees(const glm::vec3 &degrees) {
  const glm::vec3 r = glm::radians(degrees);
  return glm::quat_cast(glm::eulerAngleYXZ(r.y, r.x, r.z));
}
// The largest difference between two rotations' axes (about the angle between them, in radians)
float apart(const glm::quat &a, const glm::quat &b) {
  const glm::mat3 ma = glm::mat3_cast(a), mb = glm::mat3_cast(b);
  float most = 0.0f;
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r)
      most = std::max(most, std::abs(ma[c][r] - mb[c][r]));
  return most;
}
} // namespace

TEST_CASE(euler_degrees_at_gimbal_lock) {
  // Stood on a wall facing Z: 90 degrees of pitch and nothing else, read back as exactly that (it read 0.194 of yaw
  // and the opposite roll before)
  CHECK(TransformSystem::eulerDegrees(glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0))) ==
        glm::vec3(90.0f, 0.0f, 0.0f));
  CHECK(TransformSystem::eulerDegrees(glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0))) ==
        glm::vec3(-90.0f, 0.0f, 0.0f));
  // A turn at 90 degrees of pitch: all in the yaw, the roll 0
  const glm::vec3 turned = TransformSystem::eulerDegrees(fromDegrees(glm::vec3(90.0f, 30.0f, 0.0f)));
  CHECK_NEAR(turned.x, 90.0f, 1e-3f);
  CHECK_NEAR(turned.y, 30.0f, 1e-3f);
  CHECK_EQ(turned.z, 0.0f);
}

TEST_CASE(euler_degrees_round_trip) {
  // Snapped angles come back as written, not 14.99997
  CHECK(TransformSystem::eulerDegrees(fromDegrees(glm::vec3(15.0f, 45.0f, -30.0f))) == glm::vec3(15.0f, 45.0f, -30.0f));
  CHECK(TransformSystem::eulerDegrees(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) == glm::vec3(0.0f)); // and 0, not -0
  // Any rotation, near 90 degrees of pitch too, comes back as the same rotation
  float worst = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    glm::vec3 degrees(static_cast<float>((i * 37) % 360 - 180), static_cast<float>((i * 91) % 360 - 180),
                      static_cast<float>((i * 53) % 360 - 180));
    if (i % 3 == 0)
      degrees.x = (i % 2 ? 90.0f : -90.0f) + static_cast<float>(i % 21 - 10) * 0.001f;
    const glm::quat rotation = fromDegrees(degrees);
    worst = std::max(worst, apart(rotation, fromDegrees(TransformSystem::eulerDegrees(rotation))));
  }
  CHECK(worst < glm::radians(0.02f));
}

TEST_CASE(child_of_a_pitched_parent_reads_clean_angles) {
  // A child placed through the hierarchy gets its world angles from a decomposed matrix: on a parent stood up at 90
  // degrees they are the parent's, not noise
  const Entity parent = engine.entities().create("Stood up").withTransform(glm::vec3(0.0f), glm::vec3(90.0f, 0.0f, 0.0f)).build();
  const Entity child = engine.entities().create("On it").withTransform(glm::vec3(0.0f, 2.0f, 0.0f)).build();
  auto &transforms = engine.getSystemManager().getSystem<TransformSystem>();
  auto &components = engine.getComponentManager();
  CHECK(transforms.setParent(components, child, parent));
  Transform local;
  local.position = glm::vec3(0.0f, 2.0f, 0.0f); // turned with the parent: its world angles are the parent's
  transforms.setLocalTransform(components, child, local);
  transforms.updateHierarchy(components);
  const Transform &placed = components.get<Transform>(child);
  CHECK_EQ(placed.rotation.x, 90.0f);
  CHECK_EQ(placed.rotation.y, 0.0f);
  CHECK_EQ(placed.rotation.z, 0.0f);
}
