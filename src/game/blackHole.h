#pragma once
// Black hole example: an orbiting particle accretion disk and a starfield, both bent by a gravitational lens.

#include "components/gravityLens.h"
#include "components/particleEmitter.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "systems/cameraSystem.h"
#include "systems/inputSystem.h"
#include <cmath>

class BlackHole : public App {
  static constexpr float kSchwarzschildRadius = 1.2f;
  static constexpr float kShadowRadius = 2.6f * kSchwarzschildRadius; // apparent size of the black disk
  static constexpr float kDiskInner = 3.0f * kSchwarzschildRadius;   // innermost stable circular orbit
  static constexpr float kDiskOuter = 14.0f;
  static constexpr float kGravity = 120.0f;

  // Orbit camera, in degrees and world units
  float azimuth = 20.0f;
  float elevation = 9.0f;
  float distance = 30.0f;
  float orbitSpeed = 4.0f;

  Entity hole = -1;

  // Particles that fall inside `resetRadius` respawn at the outer edge, so the disk keeps feeding itself.
  void configureDisk(ParticleEmitter &p, int count, float size, glm::vec4 outerColor, glm::vec4 innerColor) {
    p.emissionShape = EmissionShape::Ring;
    p.ringInnerRadius = kDiskInner;
    p.ringOuterRadius = kDiskOuter;
    p.ringThickness = 0.05f;
    p.emitRate = 1.0e6f; // fill the whole disk on the first frame
    p.maxParticles = count;
    p.particleLifetime = 1.0e9f;
    p.size = size;
    p.sizeDecay = 0.0f;
    p.gravity = glm::vec3(0.0f);

    p.attractMode = true;
    p.attractPointRelative = true;
    p.attractStrength = kGravity;
    p.emitTangentially = true;
    p.tangentStrength = 1.0f;
    p.tangentVariance = 0.03f;
    p.friction = 0.02f;
    p.resetRadius = kDiskInner - 0.2f;
    p.enableRecycling = true;

    p.distBasedColor = true;
    p.colorRadius = kDiskOuter;
    p.startColor = outerColor;
    p.endColor = innerColor;
    p.additiveBlending = true;
  }

  void configureStars(ParticleEmitter &p, int count, float size, glm::vec4 color) {
    p.emissionShape = EmissionShape::Sphere;
    p.sphereRadius = 62.0f; // stays inside the camera's far plane
    p.sphereSurfaceOnly = true;
    p.emitRate = 1.0e6f;
    p.maxParticles = count;
    p.particleLifetime = 1.0e9f;
    p.speed = 0.0f;
    p.speedVariance = 0.0f;
    p.size = size;
    p.sizeDecay = 0.0f;
    p.gravity = glm::vec3(0.0f);
    p.startColor = color;
    p.endColor = color;
    p.additiveBlending = true;
  }

  void createStars(Engine &engine) {
    Entity dim = engine.entities()
                     .create("Stars")
                     .withTransform(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f))
                     .withComponent<ParticleEmitter>()
                     .build();
    configureStars(engine.getOrThrow<ParticleEmitter>(dim), 6000, 0.45f, {0.75f, 0.8f, 1.0f, 0.55f});

    Entity bright = engine.entities()
                        .create("Bright Stars")
                        .withTransform(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f))
                        .withComponent<ParticleEmitter>()
                        .build();
    configureStars(engine.getOrThrow<ParticleEmitter>(bright), 400, 0.9f, {1.0f, 0.92f, 0.8f, 0.9f});
  }

  void createBlackHole(Engine &engine) {
    // The black sphere is the shadow: it hides the images that fall inside the photon sphere
    // Its drone gets louder as the camera closes in
    hole = engine.entities()
               .create("Black Hole")
               .withModel(EngineConfig::MODEL_SPHERE, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(kShadowRadius))
               .withSolidColor(glm::vec3(0.0f))
               .withComponent<GravityLens>(kSchwarzschildRadius)
               .withSound("../assets/sounds/blackhole.flac", true, 1.0f)
               .build();
    auto &hum = engine.getOrThrow<AudioSource>(hole);
    hum.minDistance = 12.0f; // the closest the camera gets
    hum.maxDistance = 40.0f;

    Entity disk = engine.entities()
                      .create("Accretion Disk")
                      .withTransform(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f))
                      .withComponent<ParticleEmitter>()
                      .build();
    configureDisk(engine.getOrThrow<ParticleEmitter>(disk), 70000, 0.1f, {1.0f, 0.3f, 0.06f, 0.35f},
                  {1.0f, 0.9f, 0.72f, 1.0f});

    // Large, faint particles on the same orbits give the disk a smooth glow
    Entity haze = engine.entities()
                      .create("Disk Glow")
                      .withTransform(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f))
                      .withComponent<ParticleEmitter>()
                      .build();
    configureDisk(engine.getOrThrow<ParticleEmitter>(haze), 16000, 0.9f, {0.9f, 0.25f, 0.04f, 0.03f},
                  {1.0f, 0.7f, 0.4f, 0.09f});
  }

  void updateCamera(Engine &engine, float deltaTime) {
    auto &input = engine.getSystemManager().getSystem<InputSystem>();
    auto &cameraSystem = engine.getSystemManager().getSystem<CameraSystem>();

    if (input.isKeyPressed(SDL_SCANCODE_UP))
      elevation += 20.0f * deltaTime;
    if (input.isKeyPressed(SDL_SCANCODE_DOWN))
      elevation -= 20.0f * deltaTime;
    if (input.isKeyPressed(SDL_SCANCODE_W))
      distance -= 8.0f * deltaTime;
    if (input.isKeyPressed(SDL_SCANCODE_S))
      distance += 8.0f * deltaTime;
    if (input.isKeyPressed(SDL_SCANCODE_LEFT))
      azimuth -= 40.0f * deltaTime;
    if (input.isKeyPressed(SDL_SCANCODE_RIGHT))
      azimuth += 40.0f * deltaTime;

    elevation = glm::clamp(elevation, -80.0f, 80.0f);
    distance = glm::clamp(distance, 12.0f, 40.0f);
    azimuth += orbitSpeed * deltaTime;

    Entity camEntity = cameraSystem.getActiveCamera();
    if (camEntity == -1)
      return;

    auto &cam = engine.getOrThrow<Camera>(camEntity);
    float az = glm::radians(azimuth);
    float el = glm::radians(elevation);
    glm::vec3 offset = distance * glm::vec3(glm::cos(el) * glm::sin(az), glm::sin(el), glm::cos(el) * glm::cos(az));

    // Look at the origin: invert the yaw/pitch convention used by CameraSystem::updateFrontVector
    glm::vec3 dir = -glm::normalize(offset);
    cam.position = offset;
    cam.yaw = glm::degrees(std::atan2(dir.x, -dir.z));
    cam.pitch = glm::degrees(std::asin(dir.y));
    cameraSystem.updateFrontVector(cam);

    // Redshift: the drone drops in pitch the deeper the camera sits in the well
    if (auto *hum = engine.getOrNil<AudioSource>(hole))
      hum->pitch = glm::mix(0.8f, 1.0f, (distance - 12.0f) / 28.0f);
  }

public:
  void setup(Engine &engine) override {
    engine.setState(Toggle::CameraMovement, false);
    engine.setState(Toggle::CursorLock, false);

    engine.createCameraEntity("Camera", glm::vec3(0.0f, 0.0f, distance), 0.0f, 0.0f, 55.0f);

    createStars(engine);
    createBlackHole(engine);
  }

  void update(Engine &engine, float deltaTime) override { updateCamera(engine, deltaTime); }
};
