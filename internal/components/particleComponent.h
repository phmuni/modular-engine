#pragma once
// Particle emitter component and per-particle data.

#include <glm/glm.hpp>
#include <vector>

struct Particle {
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  glm::vec4 color{1.0f};
  float life = 0.0f;
  float maxLife = 1.0f;
  float size = 0.1f;
};

struct ParticleComponent {
  float emitRate = 10.0f;        // particles per second
  float particleLifetime = 2.0f; // seconds
  float speed = 2.0f;
  float speedVariance = 0.5f;
  float size = 0.15f;
  float sizeDecay = 1.0f; // 0 = none, 1 = fast
  glm::vec3 gravity{0.0f, -1.0f, 0.0f};
  glm::vec4 startColor{1.0f, 0.8f, 0.2f, 1.0f};
  glm::vec4 endColor{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 emitDirection{0.0f, 1.0f, 0.0f};
  glm::vec3 offset{0.0f}; // position relative to entity transform
  float spread = 0.5f;    // 0 = straight, 1 = hemisphere
  int maxParticles = 500;
  bool active = true;
  bool additiveBlending = true;

  // Runtime state
  float emitAccumulator = 0.0f;
  std::vector<Particle> particles;
};
