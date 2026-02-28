#include "foundation/core/config.h"
#include "foundation/core/engine.h"

void createDefaultModel(const std::string &name, Engine &engine, glm::vec3 position = glm::vec3(0.0f),
                        glm::vec3 scale = glm::vec3(1.0f));
void createDirectionalLight(const std::string &name, Engine &engine);
void createCamera(Engine &engine);

int main() {
  Engine engine;

  if (!engine.init())
    return 1;

  // Setup default scene
  createDefaultModel("Object", engine, glm::vec3(0.0f), glm::vec3(1.0f));
  createDirectionalLight("Directional", engine);
  createCamera(engine);

  engine.run();
  return 0;
}

void createDefaultModel(const std::string &name, Engine &engine, glm::vec3 position, glm::vec3 scale) {
  glm::vec3 rotation(0.0f);
  engine.createModelEntity(name, EngineConfig::MODEL_BOX, position, rotation, scale);
}

void createDirectionalLight(const std::string &name, Engine &engine) {
  glm::vec3 position(2.0f, 3.0f, 2.0f);
  glm::vec3 direction(-1.0f, -1.0f, -1.0f);
  glm::vec3 color(1.0f);
  float intensity = 1.5f;

  engine.createLightEntity(name, position, direction, color, LightType::Directional, intensity, 0.0f, 0.0f);
}

void createCamera(Engine &engine) {
  glm::vec3 position(0.0f, 3.0f, 8.0f);
  engine.createCameraEntity(position, 0.0f, -15.0f, 90.0f);
}
