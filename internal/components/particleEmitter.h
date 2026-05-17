#pragma once
// Particle emitter component for creating and managing particle systems.

#include <glm/glm.hpp>
#include <vector>

struct Particle {
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  glm::vec4 color{1.0f};
  float life = 0.0f;
  float maxLife = 1.0f;
  float size = 0.1f;

  Particle() = default;
};

struct ParticleEmitter {
  float emitRate = 10.0f;
  bool emitTangentially = false;
  float tangentVariance = 0.0f;
  float particleLifetime = 2.0f;
  float speed = 2.0f;
  float speedVariance = 0.5f;
  float size = 0.15f;
  float sizeDecay = 1.0f;
  glm::vec3 gravity = glm::vec3(0.0f, -1.0f, 0.0f);
  glm::vec4 startColor = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
  glm::vec4 endColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 emitDirection = glm::vec3(0.0f, 1.0f, 0.0f);
  glm::vec3 offset = glm::vec3(0.0f);
  float spread = 0.5f;
  int maxParticles = 500;
  bool active = true;
  bool additiveBlending = true;

  bool attractMode = false;
  glm::vec3 attractPoint = glm::vec3(0.0f);
  float attractStrength = 0.0f;
  float tangentStrength = 0.0f;
  float resetRadius = 0.0f;
  bool enableRecycling = true;
  bool distBasedColor = false;
  float colorRadius = 1.0f;
  float friction = 0.0f;

  float emitAccumulator = 0.0f;
  std::vector<Particle> particles;

  ParticleEmitter() = default;
};