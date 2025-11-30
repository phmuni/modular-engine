#include "core/config.h"
#include "core/engine.h"

// Forward declarations
void createDefaultModel(const std::string &name, Engine &engine, glm::vec3 position = glm::vec3(0.0f));
void createSpotlight(const std::string &name, Engine &engine);
void createDirectionalLight(const std::string &name, Engine &engine);
void createCamera(Engine &engine);

int main() {
  Engine engine;

  if (!engine.initialize()) {
    SDL_Log("Failed to initialize engine!");
    return 1;
  }

  glClearColor(EngineConfig::DEFAULT_CLEAR_COLOR_R, EngineConfig::DEFAULT_CLEAR_COLOR_G,
               EngineConfig::DEFAULT_CLEAR_COLOR_B, EngineConfig::DEFAULT_CLEAR_COLOR_A);

  createDefaultModel("Backpack", engine, glm::vec3(0.0f));
  createDefaultModel("Backpack2", engine, glm::vec3(1.0f));
  createDirectionalLight("Directional 1", engine);
  createCamera(engine);

  engine.run();
  return 0;
}

void createDefaultModel(const std::string &name, Engine &engine, glm::vec3 position) {
  glm::vec3 rotation(0.0f);
  glm::vec3 scale(1.0f);

  engine.createEntityModel(name, EngineConfig::MODEL_BACKPACK, EngineConfig::TEXTURE_BACKPACK, position, rotation,
                           scale);
}

void createSpotlight(const std::string &name, Engine &engine) {
  glm::vec3 position(3.0f, 3.0f, 0.0f);
  glm::vec3 direction(-1.0f, -1.0f, 0.0f);
  glm::vec3 color(1.0f);

  float intensity = 1.5f;
  float cutOff = glm::cos(glm::radians(15.0f));
  float outerCutOff = glm::cos(glm::radians(25.0f));

  engine.createEntityLight(name, position, direction, color, LightType::Spot, intensity, cutOff, outerCutOff);
}

void createDirectionalLight(const std::string &name, Engine &engine) {
  glm::vec3 position(3.0f, 3.0f, 0.0f);
  glm::vec3 direction(-1.0f, -1.0f, 0.0f);
  glm::vec3 color(1.0f);
  float intensity = 1.5f;

  float cutOff = 0.0f;
  float outerCutOff = 0.0f;

  engine.createEntityLight(name, position, direction, color, LightType::Directional, intensity, cutOff, outerCutOff);
}

void createCamera(Engine &engine) {
  glm::vec3 position(0.0f, 3.0f, 8.0f);
  float yaw = 0.0f;
  float pitch = -15.0f;
  float fov = 90.0f;

  engine.createEntityCamera(position, yaw, pitch, fov);
}
