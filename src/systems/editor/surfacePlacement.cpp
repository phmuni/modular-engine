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

glm::vec3 grabPoint(const glm::vec3 &position, const glm::quat &rotation, const glm::vec3 &support,
                    float supportHeight, const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection) {
  const float facing = glm::dot(rayDirection, support);
  if (std::abs(facing) < 1e-4f)
    return glm::vec3(0.0f);
  const glm::vec3 base = position - support * supportHeight;
  const float along = glm::dot(base - rayOrigin, support) / facing;
  if (along <= 0.0f)
    return glm::vec3(0.0f);
  return glm::inverse(rotation) * (rayOrigin + rayDirection * along - position);
}

glm::vec3 place(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax, const glm::vec3 &scale,
                const glm::quat &rotation, const glm::vec3 &grabLocal, const glm::vec3 &hitPoint,
                const glm::vec3 &hitNormal) {
  const glm::vec3 grabbed = rotation * grabLocal;
  const glm::vec3 along = grabbed - hitNormal * glm::dot(grabbed, hitNormal);
  return hitPoint - along + hitNormal * supportDistance(boundsMin, boundsMax, scale, rotation, hitNormal);
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
