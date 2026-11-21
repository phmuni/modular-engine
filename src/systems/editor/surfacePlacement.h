#pragma once
// The geometry of the editor's "Align to surface" drag, apart from the editor: where the grabbed point of an entity
// is, how far its bounds reach below it along a surface's normal, where it is put on a surface, and a point put on the
// grid of the surface's own model. Pure math on vectors, so it is tested without a window.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace surfacePlacement {

// How far the origin sits from a surface facing `normal` (world space) when the entity rests on it: the largest
// distance below the origin, along the normal, of the 8 corners of its bounds (scaled, then rotated)
float supportDistance(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax, const glm::vec3 &scale,
                      const glm::quat &rotation, const glm::vec3 &normal);

// The point of the entity the cursor holds, in its own frame (rotated, not scaled, origin at its position). It is where
// the cursor's ray meets the plane of the entity's support: through its base point (the origin lowered by
// `supportHeight` along `support`), facing `support`. A ray parallel to that plane, or meeting it behind the ray's
// origin, grabs the origin itself (no offset).
glm::vec3 grabPoint(const glm::vec3 &position, const glm::quat &rotation, const glm::vec3 &support,
                    float supportHeight, const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection);

// The origin that rests the entity (rotated by `rotation`) on a surface at `hitPoint` facing `hitNormal`, with the
// grabbed point (`grabLocal`, from grabPoint) on the hit: the part of the rotated grab point along the surface is
// taken off, and the origin is lifted by the support distance along the normal
glm::vec3 place(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax, const glm::vec3 &scale,
                const glm::quat &rotation, const glm::vec3 &grabLocal, const glm::vec3 &hitPoint,
                const glm::vec3 &hitNormal);

// A point on a surface (through `point`, facing `normal`) put on the grid of a frame (a model's rotation and
// position): in that frame the two axes the surface faces least are rounded to `step`, then it is brought back onto
// the surface along its normal
glm::vec3 snapInFrame(const glm::vec3 &point, const glm::vec3 &normal, const glm::quat &frameRotation,
                      const glm::vec3 &frameOrigin, float step);

} // namespace surfacePlacement
