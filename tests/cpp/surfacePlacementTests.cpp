// The geometry of the editor's Align to surface drag (systems/editor/surfacePlacement.h): resting on a surface, holding the
// gizmo's center on the cursor, sliding back onto a surface, and the grid of the surface's own model.

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
  // Upright on a floor: the origin rests -boundsMin.y * scale.y above it
  const glm::vec3 scale(1.0f, 3.0f, 1.0f);
  CHECK_NEAR(supportDistance(kMin, kMax, scale, kNone, kUp), 1.5f, 1e-5f);
}

TEST_CASE(surface_placement_keep_rotation_on_its_side) {
  // Turned 90 degrees about Z, the 2 long side is vertical: it rests on its 1 wide face, the origin 1 above
  const glm::quat onSide = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
  CHECK_NEAR(supportDistance(kMin, kMax, glm::vec3(1.0f), onSide, kUp), 1.0f, 1e-5f);
  // Upright, the same box is 0.5 above
  CHECK_NEAR(supportDistance(kMin, kMax, glm::vec3(1.0f), kNone, kUp), 0.5f, 1e-5f);
}

TEST_CASE(surface_placement_held_origin_on_the_cursor) {
  // The cursor's ray from (0, 5, 5) toward the floor: the origin, 0.5 above it, is where the ray is 0.5 high
  const glm::vec3 eye(0, 5, 5), toward = glm::normalize(glm::vec3(0, -1, -1));
  glm::vec3 origin;
  CHECK(heldOrigin(eye, toward, glm::vec3(0, 0, 0), kUp, 0.5f, origin));
  CHECK_NEAR(origin, glm::vec3(0, 0.5f, 0.5f), 1e-5f);
  // Along the floor, or meeting the plane behind the eye, it cannot be held
  CHECK(!heldOrigin(eye, glm::vec3(0, 0, -1), glm::vec3(0, 0, 0), kUp, 0.5f, origin));
  CHECK(!heldOrigin(glm::vec3(0, 0.2f, 0), toward, glm::vec3(0, 0, 0), kUp, 0.5f, origin));
}

TEST_CASE(surface_placement_grab_at_the_origin_is_the_held_origin) {
  const glm::vec3 eye(0, 5, 5), toward = glm::normalize(glm::vec3(0, -1, -1));
  glm::vec3 held, grabbed;
  CHECK(heldOrigin(eye, toward, glm::vec3(0, 0, 0), kUp, 0.5f, held));
  CHECK(grabbedOrigin(eye, toward, glm::vec3(0, 0, 0), kUp, 0.5f, glm::vec3(0.0f), grabbed));
  CHECK_NEAR(grabbed, held, 1e-6f);
}

TEST_CASE(surface_placement_grab_keeps_the_grabbed_point_on_the_ray) {
  // A pillar 3 high (origin at mid-height, 1.5 above the floor), grabbed 1.5 below its origin and 0.3 to the side:
  // that point rests on the floor, under the ray, and the origin is 1.5 over it
  const glm::vec3 eye(0, 5, 5), toward = glm::normalize(glm::vec3(0, -1, -1));
  const glm::vec3 grab(0.3f, -1.5f, 0.0f);
  glm::vec3 origin;
  CHECK(grabbedOrigin(eye, toward, glm::vec3(0, 0, 0), kUp, 1.5f, grab, origin));
  const glm::vec3 grabbedPoint = origin + grab;
  CHECK_NEAR(grabbedPoint.y, 0.0f, 1e-5f);
  CHECK_NEAR(origin.y, 1.5f, 1e-5f);
  CHECK_NEAR(glm::normalize(grabbedPoint - eye), toward, 1e-5f); // on the ray
}

TEST_CASE(surface_placement_grab_follows_the_rotation) {
  // The same grab point of an entity turned 90 degrees about Z: its offset in the world is the turned one, and the
  // grabbed point is level with the origin
  const glm::quat turned = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
  const glm::vec3 grab = turned * glm::vec3(0.0f, -1.5f, 0.0f);
  const glm::vec3 eye(0, 5, 5), toward = glm::normalize(glm::vec3(0, -1, -1));
  glm::vec3 origin;
  CHECK(grabbedOrigin(eye, toward, glm::vec3(0, 0, 0), kUp, 0.5f, grab, origin));
  CHECK_NEAR(origin.y, 0.5f, 1e-4f);
  CHECK_NEAR((origin + grab).y, 0.5f, 1e-4f);
  CHECK_NEAR(glm::normalize(origin + grab - eye), toward, 1e-5f);
}

TEST_CASE(surface_placement_slide_to_support) {
  // A pillar whose top reaches z = 0.5: the held center at z = 2 is off it, so it slides back towards the spot the
  // cursor meets (z = -1) until it is over the top again
  const auto onPillar = [](const glm::vec3 &at) { return at.z <= 0.5f; };
  CHECK_NEAR(slideToSupport(glm::vec3(0, 3.5f, 2), glm::vec3(0, 3.5f, -1), onPillar), glm::vec3(0, 3.5f, 0.5f), 1e-3f);
  // Over the top already, the held center is kept
  CHECK_NEAR(slideToSupport(glm::vec3(0, 3.5f, 0.2f), glm::vec3(0, 3.5f, -1), onPillar), glm::vec3(0, 3.5f, 0.2f), 1e-6f);
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
