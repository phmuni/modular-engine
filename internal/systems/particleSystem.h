#pragma once
// Particle emission, simulation, and render dispatch.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <cstdint>
#include <glm/glm.hpp>

struct ParticleEmitter;

class ParticleSystem : public BaseSystem {
public:
  ParticleSystem() = default;
  ~ParticleSystem() = default;

  void setShaderHandle(uint32_t handle) { m_shaderHandle = handle; }

  void update(float deltaTime, ComponentManager &componentManager);
  void render(SystemManager &systemManager, ComponentManager &componentManager);

private:
  uint32_t m_shaderHandle = 0;

  void emitParticles(ParticleEmitter &emitter, const glm::vec3 &origin, float deltaTime);
  void updateParticles(ParticleEmitter &emitter, float deltaTime);
};
