// Particle system: emission, physics simulation, and GPU render dispatch.
#include "systems/particleSystem.h"
#include "components/cameraComponent.h"
#include "components/particleComponent.h"
#include "components/transformComponent.h"
#include "rendering/renderer.h"
#include "rendering/resources/shader.h"
#include "systems/cameraSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {
std::mt19937 rng{std::random_device{}()};
std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
std::uniform_real_distribution<float> distSym(-1.0f, 1.0f);
} // namespace

void ParticleSystem::emitParticles(ParticleComponent &emitter, const glm::vec3 &origin, float deltaTime) {
  if (!emitter.active)
    return;

  emitter.emitAccumulator += emitter.emitRate * deltaTime;
  int toEmit = static_cast<int>(emitter.emitAccumulator);
  emitter.emitAccumulator -= static_cast<float>(toEmit);

  int available = emitter.maxParticles - static_cast<int>(emitter.particles.size());
  toEmit = std::min(toEmit, available);

  for (int i = 0; i < toEmit; ++i) {
    Particle p;
    p.position = origin;

    // Random direction within cone around emitDirection
    glm::vec3 dir = glm::normalize(emitter.emitDirection);
    glm::vec3 randomOffset(distSym(rng), distSym(rng), distSym(rng));
    dir = glm::normalize(dir + randomOffset * emitter.spread);

    float speed = emitter.speed + distSym(rng) * emitter.speedVariance;
    p.velocity = dir * speed;

    p.maxLife = emitter.particleLifetime + distSym(rng) * emitter.particleLifetime * 0.2f;
    p.life = p.maxLife;
    p.size = emitter.size + distSym(rng) * emitter.size * 0.3f;
    p.color = emitter.startColor;

    emitter.particles.push_back(p);
  }
}

void ParticleSystem::updateParticles(ParticleComponent &emitter, float deltaTime) {
  for (auto &p : emitter.particles) {
    p.life -= deltaTime;
    p.velocity += emitter.gravity * deltaTime;
    p.position += p.velocity * deltaTime;

    // Interpolate color over lifetime
    float t = 1.0f - (p.life / p.maxLife);
    p.color = glm::mix(emitter.startColor, emitter.endColor, t);

    // Size decay
    if (emitter.sizeDecay > 0.0f) {
      p.size *= (1.0f - emitter.sizeDecay * deltaTime);
      if (p.size < 0.001f)
        p.size = 0.001f;
    }
  }

  // Remove dead particles
  emitter.particles.erase(std::remove_if(emitter.particles.begin(), emitter.particles.end(),
                                         [](const Particle &p) { return p.life <= 0.0f; }),
                          emitter.particles.end());
}

void ParticleSystem::update(float deltaTime, ComponentManager &componentManager) {
  componentManager.each<ParticleComponent>([&](Entity entity, ParticleComponent &emitter) {
    auto *transform = componentManager.tryGet<TransformComponent>(entity);
    glm::vec3 origin = (transform ? transform->position : glm::vec3(0.0f)) + emitter.offset;

    emitParticles(emitter, origin, deltaTime);
    updateParticles(emitter, deltaTime);
  });
}

void ParticleSystem::render(SystemManager &systemManager, ComponentManager &componentManager) {
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &renderer = systemManager.getSystem<RenderSystem>().getRenderer();

  Entity camEntity = cameraSystem.getActiveCamera();
  if (camEntity == -1)
    return;

  const auto &cam = componentManager.get<CameraComponent>(camEntity);
  glm::mat4 view = cameraSystem.getViewMatrix(cam);
  glm::mat4 projection = cameraSystem.getProjMatrix(cam);

  // Collect all alive particles into vertex data
  std::vector<ParticleVertex> vertices;
  bool useAdditive = true;

  componentManager.each<ParticleComponent>([&](Entity entity, ParticleComponent &emitter) {
    useAdditive = emitter.additiveBlending;
    for (const auto &p : emitter.particles) {
      if (p.life > 0.0f) {
        vertices.push_back({p.position, p.color, p.size});
      }
    }
  });

  if (vertices.empty())
    return;

  // Shader setup
  Shader &shader = resourceSystem.getShader(m_shaderHandle);
  shader.use();
  shader.setMat4("view", view);
  shader.setMat4("projection", projection);

  // Delegate all GL state + drawing to Renderer
  int count = static_cast<int>(vertices.size());
  renderer.beginParticlePass(useAdditive);
  renderer.uploadParticles(vertices.data(), count);
  renderer.drawParticles(count);
  renderer.endParticlePass();
}
