#include "foundation/core/engine.h"

class TestApp : public App {
  Entity box;

  void setup(Engine &engine) override {
    box = engine.createModelEntity("Box", EngineConfig::MODEL_BOX, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f),
                                   glm::vec3(1.0f));

    engine.createLightEntity("Directional", glm::vec3(2.0f, 3.0f, 2.0f), glm::vec3(-1.0f, -1.0f, -1.0f),
                             glm::vec3(1.0f), LightType::Directional, 1.5f, 0.0f, 0.0f);

    engine.createCameraEntity(glm::vec3(0.0f, 0.0f, 5.0f), 0.0f, 0.0f, 90.0f);
  }

  void update(Engine &engine, float deltaTime) override {}
};

int main() {
  Engine engine;

  if (!engine.init())
    return 1;

  TestApp application;
  engine.run(application);

  return 0;
}
