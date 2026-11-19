#pragma once
// Model viewer: loads any OBJ/GLB given on the command line and orbits a camera around it.

#include "components/camera.h"
#include "components/model.h"
#include "foundation/core/engine.h"
#include "rendering/resources/mesh.h"
#include "systems/inputSystem.h"
#include "systems/resourceSystem.h"
#include <cmath>
#include <string>
#include <utility>

class Viewer : public App {
  std::string m_path;
  glm::vec3 m_target{0.0f};
  float m_radius = 1.0f;

  float azimuth = 35.0f;
  float elevation = 20.0f;
  float distance = 3.0f;
  float orbitSpeed = 10.0f;

  void updateCamera(Engine &engine, float deltaTime) {
    auto &input = engine.input();

    if (input.isKeyDown(SDL_SCANCODE_UP))
      elevation += 30.0f * deltaTime;
    if (input.isKeyDown(SDL_SCANCODE_DOWN))
      elevation -= 30.0f * deltaTime;
    if (input.isKeyDown(SDL_SCANCODE_LEFT))
      azimuth -= 60.0f * deltaTime;
    if (input.isKeyDown(SDL_SCANCODE_RIGHT))
      azimuth += 60.0f * deltaTime;
    if (input.isKeyDown(SDL_SCANCODE_W))
      distance *= 1.0f - 0.8f * deltaTime;
    if (input.isKeyDown(SDL_SCANCODE_S))
      distance *= 1.0f + 0.8f * deltaTime;

    elevation = glm::clamp(elevation, -85.0f, 85.0f);
    distance = glm::clamp(distance, m_radius * 0.2f, m_radius * 20.0f);
    azimuth += orbitSpeed * deltaTime;

    Camera *camera = engine.getActiveCamera();
    if (!camera)
      return;

    float az = glm::radians(azimuth);
    float el = glm::radians(elevation);
    glm::vec3 offset = distance * glm::vec3(glm::cos(el) * glm::sin(az), glm::sin(el), glm::cos(el) * glm::cos(az));
    camera->lookAt(m_target + offset, m_target);
  }

public:
  explicit Viewer(std::string path) : m_path(std::move(path)) {}

  void setup(Engine &engine) override {
    engine.setState(Toggle::CameraMovement, false);
    engine.setState(Toggle::CursorLock, false);

    engine.entities().create("Sun").withLight(Light::directional({-0.4f, -1.0f, -0.6f})).build();
    engine.entities()
        .create("Fill")
        .withLight(Light::directional({0.6f, -0.3f, 0.7f}, {0.55f, 0.6f, 0.7f}, 0.5f))
        .build();

    Entity model = engine.entities()
                       .create("Model")
                       .withModel(m_path, glm::vec3(0.0f))
                       .build();

    // Scale the model to a 2-unit radius so the camera's fixed near/far planes always fit it
    auto &resources = engine.getSystemManager().getSystem<ResourceSystem>();
    const Mesh &mesh = resources.getMesh(engine.get<Model>(model).meshHandle);
    float boundsRadius = glm::max(mesh.getBoundsRadius(), 1e-4f);
    float scale = 2.0f / boundsRadius;

    auto &transform = engine.get<Transform>(model);
    transform.scale = glm::vec3(scale);
    transform.position = -mesh.getBoundsCenter() * scale;

    m_radius = 2.0f;
    distance = m_radius * 2.6f;
    engine.createCameraEntity("Camera", glm::vec3(0.0f, 0.0f, distance), 0.0f, 0.0f, 50.0f);
  }

  // A loaded scene brings its own camera; stop orbiting it around this example's target
  void onSceneLoaded(Engine &engine, const std::string &) override {
    sceneReplaced = true;
    engine.setState(Toggle::CameraMovement, true);
  }

  void update(Engine &engine, float deltaTime) override {
    if (!sceneReplaced)
      updateCamera(engine, deltaTime);
  }

private:
  bool sceneReplaced = false;
};
