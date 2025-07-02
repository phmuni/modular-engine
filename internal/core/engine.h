#pragma once

// External Includes
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Internal Includes

#include "loader/materialLoader.h" // IWYU pragma: keep
#include "loader/meshLoader.h"     // IWYU pragma: keep

#include "manager/cameraManager.h" // IWYU pragma: keep
#include "manager/shaderManager.h"

// ECS & Systems
#include "ecs/componentManager.h"
#include "ecs/entityManager.h"
#include "ecs/systemManager.h"

#include "system/cameraSystem.h"    // IWYU pragma: keep
#include "system/inputSystem.h"     // IWYU pragma: keep
#include "system/lightSystem.h"     // IWYU pragma: keep
#include "system/renderSystem.h"    // IWYU pragma: keep
#include "system/timeSystem.h"      // IWYU pragma: keep
#include "system/transformSystem.h" // IWYU pragma: keep
#include "system/uiSystem.h"        // IWYU pragma: keep

#include "component/modelComponent.h" // IWYU pragma: keep
#include "manager/windowManager.h"

class Engine {
private:
  float m_screenWidth;
  float m_screenHeight;

  EntityManager entityManager;
  ComponentManager componentManager;
  SystemManager systemManager;

  ShaderManager shaderManager;
  WindowManager windowManager;
  ActiveCameraManager cameraManager;

  void update(bool &running);
  void render();
  void mainLoop(bool &running);
  bool loadShaderOrLog(const std::string &name, const std::string &vertexPath, const std::string &fragmentPath);
  bool loadResources();

public:
  Engine() : m_screenWidth(1280.0f), m_screenHeight(720.0f) {}
  ~Engine();

  bool initialize();
  void registerSystems();
  void run();

  void createCamera(glm::vec3 position, float yaw = 0.0f, float pitch = 0.0f, float fov = 90.0f);

  void createEntityWithModel(const std::string name, const std::string &modelPath, const std::string &texturePath,
                             glm::vec3 position, glm::vec3 rotation, glm::vec3 scale);

  void createEntityWithLight(glm::vec3 position, glm::vec3 direction, glm::vec3 color, LightType type, float intensity,
                             float cutOff, float outerCutOff);
};
