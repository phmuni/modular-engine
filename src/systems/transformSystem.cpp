
// Transform system implementation for calculating the model matrix from position, rotation, and scale.

#include "systems/transformSystem.h"
#include "glm/ext/matrix_transform.hpp"
#include <glm/gtc/quaternion.hpp>

glm::mat4 TransformSystem::calculateModelMatrix(const Transform &transform) {
  glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), transform.position);

  glm::quat qx = glm::angleAxis(glm::radians(transform.rotation.x), glm::vec3(1, 0, 0));
  glm::quat qy = glm::angleAxis(glm::radians(transform.rotation.y), glm::vec3(0, 1, 0));
  glm::quat qz = glm::angleAxis(glm::radians(transform.rotation.z), glm::vec3(0, 0, 1));

  glm::quat rotationQuat = qy * qx * qz;

  modelMatrix *= glm::mat4_cast(rotationQuat);
  modelMatrix = glm::scale(modelMatrix, transform.scale);

  return modelMatrix;
}
