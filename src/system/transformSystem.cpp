#include "system/transformSystem.h"
#include "component/transformComponent.h"

glm::mat4 TransformSystem::calculateModelMatrix(const TransformComponent &transform) {
  glm::mat4 modelMatrix = glm::mat4(1.0f);
  modelMatrix = glm::translate(modelMatrix, transform.position);
  modelMatrix = glm::rotate(modelMatrix, glm::radians(transform.rotation.x), glm::vec3(1, 0, 0));
  modelMatrix = glm::rotate(modelMatrix, glm::radians(transform.rotation.y), glm::vec3(0, 1, 0));
  modelMatrix = glm::rotate(modelMatrix, glm::radians(transform.rotation.z), glm::vec3(0, 0, 1));
  modelMatrix = glm::scale(modelMatrix, transform.scale);
  return modelMatrix;
}