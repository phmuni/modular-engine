// Application entry point and demo scene setup.
#include "components/modelComponent.h"
#include "foundation/core/engine.h"
#include "rendering/resources/material.h"
#include "systems/resourceSystem.h"

class TestApp : public App {
  Entity box;

  void setup(Engine &engine) override {
    box = engine.createModelEntity("Box", EngineConfig::MODEL_BOX, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f),
                                   glm::vec3(1.0f));

    engine.createLightEntity("Directional", glm::vec3(2.0f, 3.0f, 2.0f), glm::vec3(-1.0f, -1.0f, -1.0f),
                             glm::vec3(1.0f), LightType::Directional, 1.5f, 0.0f, 0.0f);

    engine.createCameraEntity(glm::vec3(0.0f, 0.0f, 5.0f), 0.0f, 0.0f, 90.0f);

    // Manual emission on the box (orange glow)
    auto &rs = engine.getSystemManager().getSystem<ResourceSystem>();
    auto &model = engine.getComponent<ModelComponent>(box);
    if (!model.materialHandles.empty()) {
      uint32_t newMat = rs.createMaterial();
      model.materialHandles[0] = newMat;
      auto &mat = rs.getMaterial(newMat);
      mat.setEmissionColor(glm::vec3(1.0f, 0.5f, 0.0f));
      mat.setEmissionStrength(1.5f);
    }

    // Particle effect attached to the box
    auto &particles = engine.addComponent<ParticleComponent>(box);
    particles.emitRate = 80.0f;
    particles.particleLifetime = 2.0f;
    particles.speed = 0.6f;
    particles.speedVariance = 0.4f;
    particles.size = 0.18f;
    particles.sizeDecay = 0.3f;
    particles.gravity = {0.0f, 0.3f, 0.0f};          // gentle float upward
    particles.startColor = {0.4f, 0.2f, 1.0f, 1.0f}; // bright purple
    particles.endColor = {0.0f, 0.8f, 1.0f, 0.0f};   // fade to cyan/transparent
    particles.emitDirection = {0.0f, 0.5f, 0.0f};
    particles.spread = 1.0f; // emit in all directions (sphere)
    particles.maxParticles = 600;
    particles.additiveBlending = true;
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
