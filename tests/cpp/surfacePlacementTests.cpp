// The geometry of the editor's Align to surface drag (systems/editor/surfacePlacement.h): resting on a surface, the grab
// point, and the grid of the surface's own model.

#include "systems/editor/surfacePlacement.h"
#include "testing.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

using namespace surfacePlacement;

namespace {
const glm::quat kNone(1.0f, 0.0f, 0.0f, 0.0f);
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
// A box 2 x 1 x 1 centered on the origin, as a mesh's bounds
const glm::vec3 kMin(-1.0f, -0.5f, -0.5f), kMax(1.0f, 0.5f, 0.5f);
} // namespace

TEST_CASE(surface_placement_flat_floor) {
  // Tilt mode on a floor: the base (-boundsMin.y * scale.y) goes on the hit point
  const glm::vec3 scale(1.0f, 3.0f, 1.0f);
  CHECK_NEAR(supportDistance(kMin, kMax, scale, kNone, kUp), 1.5f, 1e-5f);
  CHECK_NEAR(place(kMin, kMax, scale, kNone, glm::vec3(0.0f), glm::vec3(4, 2, -1), kUp), glm::vec3(4, 3.5f, -1), 1e-5f);
}

TEST_CASE(surface_placement_keep_rotation_on_its_side) {
  // Turned 90 degrees about Z, the 2 long side is vertical: it rests on its 1 wide face, the origin 1 above
  const glm::quat onSide = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
  CHECK_NEAR(supportDistance(kMin, kMax, glm::vec3(1.0f), onSide, kUp), 1.0f, 1e-5f);
  CHECK_NEAR(place(kMin, kMax, glm::vec3(1.0f), onSide, glm::vec3(0.0f), glm::vec3(0, 0, 0), kUp), glm::vec3(0, 1, 0), 1e-5f);
  // Upright, the same box is 0.5 above
  CHECK_NEAR(supportDistance(kMin, kMax, glm::vec3(1.0f), kNone, kUp), 0.5f, 1e-5f);
}

TEST_CASE(surface_placement_grab_point_lands_on_hit) {
  // The entity is at (0, 0.5, 0) on the floor; the cursor's ray meets the floor at (1, 0, 0.5), not under the origin
  const glm::quat rotation = glm::angleAxis(glm::radians(30.0f), kUp);
  const glm::vec3 eye(1.0f, 5.0f, 5.5f), toward = glm::normalize(glm::vec3(1, 0, 0.5f) - eye);
  const glm::vec3 grab = grabPoint(glm::vec3(0, 0.5f, 0), rotation, kUp, 0.5f, eye, toward);
  CHECK_NEAR(glm::vec3(rotation * grab) + glm::vec3(0, 0.5f, 0), glm::vec3(1, 0, 0.5f), 1e-4f);
  // The first frame: the cursor still on that spot, nothing moves
  CHECK_NEAR(place(kMin, kMax, glm::vec3(1.0f), rotation, grab, glm::vec3(1, 0, 0.5f), kUp), glm::vec3(0, 0.5f, 0), 1e-4f);
  // Turned by 90 degrees about the normal, the grabbed point is still where the cursor is, on the surface
  const glm::quat turned = glm::angleAxis(glm::radians(90.0f), kUp) * rotation;
  const glm::vec3 where = place(kMin, kMax, glm::vec3(1.0f), turned, grab, glm::vec3(1, 0, 0.5f), kUp);
  CHECK_NEAR(where + glm::vec3(turned * grab), glm::vec3(1, 0, 0.5f), 1e-4f);
  // A ray parallel to the plane grabs the origin
  CHECK_NEAR(grabPoint(glm::vec3(0, 0.5f, 0), rotation, kUp, 0.5f, eye, glm::vec3(0, 0, -1)), glm::vec3(0.0f), 1e-6f);
}

TEST_CASE(surface_placement_snap_in_target_frame) {
  // A surface turned 45 degrees about Y: its top is snapped along its own axes, not the world's
  const glm::quat frame = glm::angleAxis(glm::radians(45.0f), kUp);
  const glm::vec3 origin(10, 0, 0);
  const glm::vec3 local(0.6f, 0.0f, -0.8f);
  const glm::vec3 snapped = snapInFrame(origin + frame * local, kUp, frame, origin, 0.5f);
  CHECK_NEAR(snapped, origin + frame * glm::vec3(0.5f, 0.0f, -1.0f), 1e-4f);
  // On a wall (normal along the frame's X), the Y and Z axes are the ones rounded, and the point stays on the wall
  const glm::vec3 wallNormal = frame * glm::vec3(1, 0, 0);
  const glm::vec3 onWall = origin + frame * glm::vec3(2.0f, 0.74f, 0.2f);
  const glm::vec3 snappedWall = snapInFrame(onWall, wallNormal, frame, origin, 0.5f);
  CHECK_NEAR(snappedWall, origin + frame * glm::vec3(2.0f, 0.5f, 0.0f), 1e-4f);
}

TEST_CASE(surface_placement_support_on_slope) {
  // A box of half extent 1 on a 45 degree slope, tilted with it: 1 above its base; kept upright, a corner reaches
  // further down the slope's normal
  const glm::vec3 half(1.0f);
  const glm::vec3 normal = glm::normalize(glm::vec3(1, 1, 0));
  const glm::quat tilt = glm::rotation(kUp, normal);
  CHECK_NEAR(supportDistance(-half, half, glm::vec3(1.0f), tilt, normal), 1.0f, 1e-4f);
  CHECK_NEAR(supportDistance(-half, half, glm::vec3(1.0f), kNone, normal), 1.4142135f, 1e-4f);
}
