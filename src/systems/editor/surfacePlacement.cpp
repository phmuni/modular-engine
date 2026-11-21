#include "systems/editor/surfacePlacement.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace surfacePlacement {

float supportDistance(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax, const glm::vec3 &scale,
                      const glm::quat &rotation, const glm::vec3 &normal) {
  // The normal in the entity's own frame: the corners are scaled there, which is how the rotated box is built
  const glm::vec3 local = glm::inverse(rotation) * normal;
  float most = -std::numeric_limits<float>::max();
  for (int corner = 0; corner < 8; ++corner) {
    const glm::vec3 c((corner & 1 ? boundsMax.x : boundsMin.x) * scale.x, (corner & 2 ? boundsMax.y : boundsMin.y) * scale.y,
                      (corner & 4 ? boundsMax.z : boundsMin.z) * scale.z);
    most = std::max(most, -glm::dot(local, c));
  }
  return most;
}

bool heldOrigin(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection, const glm::vec3 &hitPoint,
                const glm::vec3 &hitNormal, float height, glm::vec3 &origin) {
  const float facing = glm::dot(rayDirection, hitNormal);
  if (std::abs(facing) < 0.05f)
    return false;
  const float along = glm::dot(hitPoint + hitNormal * height - rayOrigin, hitNormal) / facing;
  if (along <= 0.0f)
    return false;
  origin = rayOrigin + rayDirection * along;
  return true;
}

glm::vec3 slideToSupport(const glm::vec3 &held, const glm::vec3 &fallback,
                         const std::function<bool(const glm::vec3 &)> &supported, int steps) {
  if (supported(held))
    return held;
  float off = 0.0f, on = 1.0f; // fractions of the way: not supported at `off`, taken as supported at `on`
  for (int step = 0; step < steps; ++step) {
    const float middle = 0.5f * (off + on);
    (supported(held + (fallback - held) * middle) ? on : off) = middle;
  }
  return held + (fallback - held) * on;
}

glm::vec3 snapInFrame(const glm::vec3 &point, const glm::vec3 &normal, const glm::quat &frameRotation,
                      const glm::vec3 &frameOrigin, float step) {
  const glm::quat inverse = glm::inverse(frameRotation);
  glm::vec3 local = inverse * (point - frameOrigin);
  const glm::vec3 magnitude = glm::abs(inverse * normal);
  const int k = magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ? 0 : magnitude.y >= magnitude.z ? 1 : 2;
  const int i = (k + 1) % 3, j = (k + 2) % 3;
  local[i] = std::round(local[i] / step) * step;
  local[j] = std::round(local[j] / step) * step;
  const glm::vec3 world = frameRotation * local + frameOrigin;
  return world - normal * glm::dot(world - point, normal);
}

} // namespace surfacePlacement
