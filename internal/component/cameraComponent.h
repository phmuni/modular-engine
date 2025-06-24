#pragma once
#include <glm/glm.hpp>

struct CameraComponent {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::vec3 front{0.0f, 0.0f, -1.0f};
  glm::vec3 up{0.0f, 1.0f, 0.0f};
  glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

  float yaw = 0.0f;
  float pitch = 0.0f;
  float moveSpeed = 3.0f;
  float mouseSensitivity = 1.0f;
  float smoothFactor = 9.0f;
  float fov = 90.0f;

  CameraComponent() = default;
};
