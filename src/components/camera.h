#pragma once
// Camera component data for view and projection.

#include <cmath>
#include <glm/glm.hpp>

struct Camera {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::vec3 front{0.0f, 0.0f, -1.0f};
  glm::vec3 up{0.0f, 1.0f, 0.0f};
  glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

  bool isRelative = false;

  float yaw = 0.0f;
  float pitch = 0.0f;
  float moveSpeed = 3.0f;
  float mouseSensitivity = 1.0f;
  float smoothFactor = 9.0f;
  float fov = 90.0f;
  float aspectRatio = 16.0f / 9.0f;

  Camera() = default;

  // After changing yaw or pitch by hand: points front along them (yaw 0 looks down -Z, positive pitch looks up)
  void updateFront() {
    float pitchRad = glm::radians(pitch);
    float yawRad = glm::radians(yaw);
    front = glm::normalize(glm::vec3(cos(pitchRad) * sin(yawRad), sin(pitchRad), -cos(pitchRad) * cos(yawRad)));
  }

  // Places the camera at `from`, facing `target` (yaw and pitch follow, so the free camera continues from there)
  void lookAt(const glm::vec3 &from, const glm::vec3 &target) {
    const glm::vec3 direction = glm::normalize(target - from);
    position = from;
    yaw = glm::degrees(std::atan2(direction.x, -direction.z));
    pitch = glm::degrees(std::asin(direction.y));
    updateFront();
  }
};
