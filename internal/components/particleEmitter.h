#pragma once
// Particle emitter component and per-particle data.

#include <glm/glm.hpp>
#include <vector>

struct Particle {
  glm::vec3 position;
  glm::vec3 velocity;
  glm::vec4 color;
  float life;
  float maxLife;
  float size;

  Particle()
      : position(0.0f), velocity(0.0f), color(1.0f), life(0.0f), maxLife(1.0f), size(0.1f) {}
};

struct ParticleEmitter {
  float emitRate;
  float particleLifetime;
  float speed;
  float speedVariance;
  float size;
  float sizeDecay;
  glm::vec3 gravity;
  glm::vec4 startColor;
  glm::vec4 endColor;
  glm::vec3 emitDirection;
  glm::vec3 offset;
  float spread;
  int maxParticles;
  bool active;
  bool additiveBlending;

  float emitAccumulator;
  std::vector<Particle> particles;

  ParticleEmitter()
      : emitRate(10.0f), particleLifetime(2.0f), speed(2.0f), speedVariance(0.5f), size(0.15f),
        sizeDecay(1.0f), gravity(0.0f, -1.0f, 0.0f), startColor(1.0f, 0.8f, 0.2f, 1.0f),
        endColor(1.0f, 0.0f, 0.0f, 0.0f), emitDirection(0.0f, 1.0f, 0.0f), offset(0.0f),
        spread(0.5f), maxParticles(500), active(true), additiveBlending(true), emitAccumulator(0.0f) {}
};