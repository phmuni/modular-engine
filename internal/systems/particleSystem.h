#pragma once
// Particle emitter system for creating and managing particle systems.

#include "components/particleEmitter.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"

class ParticleSystem : public BaseSystem {
public:
  ParticleSystem(ComponentManager &cm) : m_componentManager(cm) {}
  ~ParticleSystem() = default;

  void setShaderHandle(uint32_t handle) { m_shaderHandle = handle; }

  void update(float deltaTime);
  void render(SystemManager &systemManager);

private:
  ComponentManager &m_componentManager;
  uint32_t m_shaderHandle = 0;

  void emitParticles(ParticleEmitter &emitter, Entity entity, const glm::vec3 &origin, float deltaTime);
  void updateParticles(ParticleEmitter &emitter, Entity entity, float deltaTime);
};
