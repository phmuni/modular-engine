#pragma once
// The geometry of the editor's "Align to surface" drag, apart from the editor: how far an entity's bounds reach below
// its origin along a surface's normal, where its origin goes to stay on the cursor, sliding it back onto a surface,
// and a point put on the grid of the surface's own model. Pure math on vectors, so it is tested without a window.

#include <glm/glm.hpp>
#include <functional>
#include <glm/gtc/quaternion.hpp>

namespace surfacePlacement {

// How far the origin sits from a surface facing `normal` (world space) when the entity rests on it: the largest
// distance below the origin, along the normal, of the 8 corners of its bounds (scaled, then rotated)
float supportDistance(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax, const glm::vec3 &scale,
                      const glm::quat &rotation, const glm::vec3 &normal);

// The origin that keeps the gizmo's center under the cursor with the entity resting on a surface (through `hitPoint`,
// facing `hitNormal`): where the cursor's ray meets the plane `height` above it (the entity's support distance).
// False when the ray runs nearly along that plane (within about 3 degrees) or meets it behind its origin.
bool heldOrigin(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection, const glm::vec3 &hitPoint,
                const glm::vec3 &hitNormal, float height, glm::vec3 &origin);

// The same for a point of the entity grabbed with the cursor: `grab` is its offset from the origin (world axes, in the
// entity's rotation as it will rest). The ray meets the plane `height` plus the grab's height along the normal above
// the surface, and the origin is that point less `grab`: the grabbed point stays on the ray, the entity on the surface.
// With a zero `grab` it is heldOrigin.
bool grabbedOrigin(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection, const glm::vec3 &hitPoint,
                   const glm::vec3 &hitNormal, float height, const glm::vec3 &grab, glm::vec3 &origin);

// The origin nearest `held` on the way from it to `fallback` for which `supported` holds: `held` itself when it does,
// else found by halving the way `steps` times (supported is taken to change once along it), `fallback` when nothing
// before it is supported
glm::vec3 slideToSupport(const glm::vec3 &held, const glm::vec3 &fallback,
                         const std::function<bool(const glm::vec3 &)> &supported, int steps = 12);

// A point on a surface (through `point`, facing `normal`) put on the grid of a frame (a model's rotation and
// position): in that frame the two axes the surface faces least are rounded to `step`, then it is brought back onto
// the surface along its normal
glm::vec3 snapInFrame(const glm::vec3 &point, const glm::vec3 &normal, const glm::quat &frameRotation,
                      const glm::vec3 &frameOrigin, float step);

} // namespace surfacePlacement
