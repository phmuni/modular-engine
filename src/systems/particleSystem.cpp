
// Particle system implementation for managing particle emitters, updating particle states, and rendering them.

#include "systems/particleSystem.h"
#include "components/transform.h"
#include "rendering/resources/shader.h"
#include "systems/cameraSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <random>

namespace {
std::mt19937 rng{std::random_device{}()};
std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
std::uniform_real_distribution<float> distSym(-1.0f, 1.0f);

glm::vec3 safeNormalize(const glm::vec3 &v, const glm::vec3 &fallback = glm::vec3(0.0f, 1.0f, 0.0f)) {
  float len = glm::length(v);
  if (len < 0.0001f)
    return fallback;
  return v / len;
}

glm::vec3 randomUnitVector() {
  float z = distSym(rng);
  float a = dist01(rng) * glm::two_pi<float>();
  float r = std::sqrt(glm::max(0.0f, 1.0f - z * z));
  return glm::vec3(r * std::cos(a), z, r * std::sin(a));
}

glm::vec3 randomSpreadDirection(const glm::vec3 &baseDir, float spread) {
  glm::vec3 randomOffset(distSym(rng), distSym(rng), distSym(rng));
  return safeNormalize(baseDir + randomOffset * spread, baseDir);
}
} // namespace

void ParticleSystem::emitParticles(ParticleEmitter &emitter, Entity entity, const glm::vec3 &origin, float deltaTime) {
  if (!emitter.active)
    return;

  emitter.emitAccumulator += emitter.emitRate * deltaTime;
  int toEmit = static_cast<int>(emitter.emitAccumulator);
  emitter.emitAccumulator -= static_cast<float>(toEmit);

  int available = emitter.maxParticles - static_cast<int>(emitter.particles.size());
  toEmit = std::min(toEmit, available);

  glm::vec3 worldAttractPoint = emitter.attractPoint;
  if (emitter.attractPointRelative) {
    auto *transform = m_componentManager.getOrNil<Transform>(entity);
    if (transform) {
      worldAttractPoint += transform->position;
    }
  }

  for (int i = 0; i < toEmit; ++i) {
    Particle p;

    glm::vec3 spawnPos = origin;
    glm::vec3 emitDir = safeNormalize(emitter.emitDirection);

    switch (emitter.emissionShape) {
    case EmissionShape::Cone: {
      emitDir = randomSpreadDirection(emitDir, emitter.spread);
      break;
    }
    case EmissionShape::Sphere: {
      glm::vec3 unit = randomUnitVector();
      float radius = glm::max(emitter.sphereRadius, 0.0f);
      float radial = radius * std::cbrt(dist01(rng));
      spawnPos = origin + unit * radial;
      emitDir = unit;
      break;
    }
    case EmissionShape::Box: {
      glm::vec3 e(glm::max(emitter.boxHalfExtents.x, 0.0f), glm::max(emitter.boxHalfExtents.y, 0.0f),
                  glm::max(emitter.boxHalfExtents.z, 0.0f));
      spawnPos += glm::vec3(distSym(rng) * e.x, distSym(rng) * e.y, distSym(rng) * e.z);
      emitDir = randomSpreadDirection(emitDir, emitter.spread);
      break;
    }
    }

    p.position = spawnPos;

    if (emitter.emitTangentially && emitter.attractMode) {
      glm::vec3 toCenter = worldAttractPoint - p.position;
      float d = glm::length(toCenter);

      glm::vec3 inward = toCenter / (d + 0.0001f);
      glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
      glm::vec3 tangDir = glm::normalize(glm::cross(inward, up));

      float orbSpeed = glm::sqrt(emitter.attractStrength / d);
      float variance = 1.0f + distSym(rng) * emitter.tangentVariance;
      p.velocity = tangDir * (orbSpeed * emitter.tangentStrength * variance);

    } else {
      float speed = emitter.speed + distSym(rng) * emitter.speedVariance;
      p.velocity = emitDir * speed;
    }

    p.maxLife = emitter.particleLifetime + distSym(rng) * emitter.particleLifetime * 0.2f;
    p.life = p.maxLife;
    p.size = emitter.size + distSym(rng) * emitter.size * 0.3f;
    p.color = emitter.startColor;

    emitter.particles.push_back(p);
  }
}

void ParticleSystem::updateParticles(ParticleEmitter &emitter, Entity entity, float deltaTime) {
  glm::vec3 worldAttractPoint = emitter.attractPoint;
  if (emitter.attractPointRelative) {
    auto *transform = m_componentManager.getOrNil<Transform>(entity);
    if (transform) {
      worldAttractPoint += transform->position;
    }
  }

  for (auto &p : emitter.particles) {
    p.life -= deltaTime;

    glm::vec3 toCenter = worldAttractPoint - p.position;
    float distSq = glm::dot(toCenter, toCenter);
    float dist = glm::sqrt(distSq);

    if (emitter.attractMode) {
      if (dist > 0.0001f) {
        glm::vec3 inward = toCenter / dist;

        float softening = 0.0001f;
        float gravityForce = emitter.attractStrength / (distSq + softening);

        p.velocity += inward * gravityForce * deltaTime;
      }

      p.velocity *= std::exp(-emitter.friction * deltaTime);

      if (dist < emitter.resetRadius) {
        if (emitter.enableRecycling && emitter.resetRadius > 0.0f && emitter.maxParticles > 0) {
          float angle = dist01(rng) * glm::two_pi<float>();

          float spawnRadius = glm::max(emitter.colorRadius, emitter.resetRadius + 1.0f);

          glm::vec3 relPos =
              glm::vec3(std::cos(angle) * spawnRadius, distSym(rng) * 0.1f, std::sin(angle) * spawnRadius);
          p.position = worldAttractPoint + relPos;

          glm::vec3 inwardRecycle = glm::normalize(relPos);
          glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
          glm::vec3 tangDir = glm::normalize(glm::cross(inwardRecycle, up));

          float orbSpeed = glm::sqrt(emitter.attractStrength / spawnRadius);

          p.velocity = tangDir * orbSpeed * emitter.tangentStrength;

          p.life = p.maxLife;
          p.size = emitter.size;
        } else {

          p.life = 0.0f;
          p.velocity = glm::vec3(0.0f);
        }
      }
    } else {
      p.velocity += emitter.gravity * deltaTime;
    }

    p.position += p.velocity * deltaTime;

    if (emitter.distBasedColor) {
      float distUpdated = glm::length(worldAttractPoint - p.position);
      float t = glm::clamp(distUpdated / emitter.colorRadius, 0.0f, 1.0f);
      p.color = glm::mix(emitter.endColor, emitter.startColor, t);
    } else {
      float t = 1.0f - (p.life / p.maxLife);
      p.color = glm::mix(emitter.startColor, emitter.endColor, t);
    }

    if (emitter.sizeDecay != 0.0f) {
      p.size *= (1.0f - emitter.sizeDecay * deltaTime);
      p.size = glm::max(p.size, 0.001f);
    }
  }

  emitter.particles.erase(std::remove_if(emitter.particles.begin(), emitter.particles.end(),
                                         [](const Particle &p) { return p.life <= 0.0f; }),
                          emitter.particles.end());
}

void ParticleSystem::update(float deltaTime) {
  m_componentManager.forEachComponent<ParticleEmitter>([&](Entity entity, ParticleEmitter &emitter) {
    auto *transform = m_componentManager.getOrNil<Transform>(entity);
    glm::vec3 origin = (transform ? transform->position : glm::vec3(0.0f)) + emitter.offset;

    emitParticles(emitter, entity, origin, deltaTime);
    updateParticles(emitter, entity, deltaTime);
  });
}

void ParticleSystem::render(SystemManager &systemManager) {
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &renderer = systemManager.getSystem<RenderSystem>().getRenderer();

  Entity camEntity = cameraSystem.getActiveCamera();
  if (camEntity == -1)
    return;

  const auto &cam = m_componentManager.getOrThrow<Camera>(camEntity);
  glm::mat4 view = cameraSystem.getViewMatrix(cam);
  glm::mat4 projection = cameraSystem.getProjectionMatrix(cam);

  std::vector<ParticleVertex> additiveVertices;
  std::vector<ParticleVertex> alphaVertices;

  m_componentManager.forEachComponent<ParticleEmitter>([&](Entity /*entity*/, ParticleEmitter &emitter) {
    auto &target = emitter.additiveBlending ? additiveVertices : alphaVertices;
    for (const auto &p : emitter.particles) {
      if (p.life > 0.0f) {
        target.push_back({p.position, p.color, p.size});
      }
    }
  });

  if (additiveVertices.empty() && alphaVertices.empty())
    return;

  Shader &shader = resourceSystem.getShader(m_shaderHandle);
  shader.use();
  shader.setMat4("view", view);
  shader.setMat4("projection", projection);

  if (!additiveVertices.empty()) {
    int count = static_cast<int>(additiveVertices.size());
    renderer.beginParticlePass(true);
    renderer.uploadParticles(additiveVertices.data(), count);
    renderer.drawParticles(count);
    renderer.endParticlePass();
  }

  if (!alphaVertices.empty()) {
    int count = static_cast<int>(alphaVertices.size());
    renderer.beginParticlePass(false);
    renderer.uploadParticles(alphaVertices.data(), count);
    renderer.drawParticles(count);
    renderer.endParticlePass();
  }
}