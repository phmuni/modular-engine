#pragma once
// Stress scene for benchmarking: a large grid of spinning, lit and shadowed boxes.

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include <cmath>
#include <string>

struct Spinner {
  glm::vec3 speed;
};

class Stress : public App {
  static constexpr int kGridSize = 50; // kGridSize^2 boxes
  static constexpr float kSpacing = 1.6f;

public:
  void setup(Engine &engine) override {
    engine.setState(Toggle::CameraMovement, false);
    engine.setState(Toggle::CursorLock, false);

    engine.entities().create("Sun").withLight(Light::directional({-0.5f, -1.0f, -0.3f}, {1.0f, 0.95f, 0.9f})).build();
    for (int i = 0; i < 4; ++i) {
      float angle = glm::radians(90.0f * static_cast<float>(i));
      glm::vec3 color(0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle), 1.0f - 0.25f * i);
      engine.entities()
          .create("Point " + std::to_string(i))
          .withTransform({std::cos(angle) * 15.0f, 4.0f, std::sin(angle) * 15.0f}, glm::vec3(0.0f), glm::vec3(1.0f))
          .withLight(Light::point(color, 3.0f))
          .build();
    }

    engine.createCameraEntity("Camera", glm::vec3(0.0f, 30.0f, 48.0f), 0.0f, -35.0f, 60.0f);

    const float half = (kGridSize - 1) * kSpacing * 0.5f;
    for (int x = 0; x < kGridSize; ++x) {
      for (int z = 0; z < kGridSize; ++z) {
        glm::vec3 position(x * kSpacing - half, 0.0f, z * kSpacing - half);
        float t = static_cast<float>(x * kGridSize + z) / static_cast<float>(kGridSize * kGridSize);
        engine.entities()
            .create("Box")
            .withModel(EngineConfig::MODEL_BOX, position, glm::vec3(0.0f), glm::vec3(0.6f))
            .withComponent<Spinner>(glm::vec3(20.0f + 60.0f * t, 45.0f - 30.0f * t, 10.0f))
            .build();
      }
    }
  }

  void update(Engine &engine, float deltaTime) override {
    engine.getComponentManager().forEachComponent<Spinner>([&](Entity entity, Spinner &spinner) {
      engine.getOrThrow<Transform>(entity).rotation += spinner.speed * deltaTime;
    });
  }
};
