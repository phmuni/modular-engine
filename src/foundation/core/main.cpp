// Application entry point and demo scene setup.
#include "foundation/core/engine.h"

class TestApp : public App {
  Entity box;
  Entity box2;

  void setup(Engine &engine) override {

    box = engine.createModelEntity("Box", EngineConfig::MODEL_BOX, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f),
                     glm::vec3(1.0f));

    // Second cube for collision test
    box2 = engine.createModelEntity("Box2", EngineConfig::MODEL_BOX, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f),
                    glm::vec3(1.0f));

    // Add CollisionComponent to both cubes
    auto &col1 = engine.addComponent<CollisionComponent>(box);
    col1.min = glm::vec3(-0.5f);
    col1.max = glm::vec3(0.5f);
    col1.isStatic = false;

    auto &col2 = engine.addComponent<CollisionComponent>(box2);
    col2.min = glm::vec3(-0.5f);
    col2.max = glm::vec3(0.5f);
    col2.isStatic = false;

    engine.createLightEntity("Directional", glm::vec3(2.0f, 3.0f, 2.0f), glm::vec3(-1.0f, -1.0f, -1.0f),
                             glm::vec3(1.0f), LightType::Directional, 1.5f, 0.0f, 0.0f);

    engine.createCameraEntity(glm::vec3(0.0f, 0.0f, 5.0f), 0.0f, 0.0f, 90.0f);

    // Manual emission on the box (orange glow)
    engine.setEmission(box, glm::vec3(1.0f, 0.5f, 0.0f), 1.5f);

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

  void update(Engine &engine, float deltaTime) override {
    // Move box2 to the left every frame
    auto &t2 = engine.getComponent<TransformComponent>(box2);
    t2.position.x -= deltaTime * 0.5f;

    // Check collision between box and box2, change emission color accordingly
    auto &collisionSystem = engine.getSystemManager().getSystem<CollisionSystem>();
    bool colliding = collisionSystem.checkEntitiesCollision(box, box2, engine.getComponentManager());
    if (colliding) {
      engine.setEmission(box, glm::vec3(1.0f, 0.0f, 0.0f), 2.0f); // red
      engine.setEmission(box2, glm::vec3(1.0f, 0.0f, 0.0f), 2.0f);
    } else {
      engine.setEmission(box, glm::vec3(0.0f, 1.0f, 0.0f), 2.0f); // green
      engine.setEmission(box2, glm::vec3(0.0f, 1.0f, 0.0f), 2.0f);
    }
  }
};

int main() {
  Engine engine;

  if (!engine.init())
    return 1;

  TestApp application;
  engine.run(application);

  return 0;
}
