// Engine implementation: system registration, resource loading, and game loop.
#include "foundation/core/engine.h"

#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/particleSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSystem.h"
#include "systems/timeSystem.h"
#include "systems/transformSystem.h"
#include "systems/uiSystem.h"
#include "systems/windowSystem.h"
#include "systems/collisionSystem.h"

#include <SDL3/SDL.h>

Engine::Engine()
    : m_screenWidth(EngineConfig::DEFAULT_SCREEN_WIDTH), m_screenHeight(EngineConfig::DEFAULT_SCREEN_HEIGHT) {}

Engine::~Engine() {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  renderSystem.getRenderer().shutdown();
  SDL_Quit();
}

bool Engine::initialize() {
  registerSystems();

  if (!loadResources()) {
    SDL_Log("Failed to load resources");
    return false;
  }

  return true;
}

void Engine::registerSystems() {
  systemManager.insert<WindowSystem>(m_screenWidth, m_screenHeight);
  systemManager.insert<StateSystem>();
  systemManager.insert<InputSystem>();
  systemManager.insert<TimeSystem>();
  systemManager.insert<ResourceSystem>();
  systemManager.insert<RenderSystem>();
  systemManager.insert<TransformSystem>();
  systemManager.insert<CameraSystem>(componentManager, systemManager.getSystem<InputSystem>());
  systemManager.insert<LightSystem>();
  systemManager.insert<SceneSystem>(entityManager, componentManager, systemManager);
  systemManager.insert<ParticleSystem>();
  systemManager.insert<CollisionSystem>();
  systemManager.insert<UISystem>(systemManager.getSystem<WindowSystem>().getWindow(),
                                 systemManager.getSystem<WindowSystem>().getContext());
}

bool Engine::loadResources() {
  auto &renderer = systemManager.getSystem<RenderSystem>().getRenderer();
  auto &windowSystem = systemManager.getSystem<WindowSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();

  renderer.init(windowSystem.getWindow());

  uint32_t baseShader = resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX),
                                                  EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT));
  uint32_t shadowShader = resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX_SHADOW),
                                                    EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT_SHADOW));

  if (baseShader != 0 || shadowShader != 1) {
    SDL_Log("Failed to load shaders");
    return false;
  }

  uint32_t particleShader =
      resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX_PARTICLE),
                                EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT_PARTICLE));

  auto &particleSystem = systemManager.getSystem<ParticleSystem>();
  particleSystem.setShaderHandle(particleShader);

  return true;
}

void Engine::run(App &app) {
  m_app = &app;
  m_app->setup(*this);

  bool running = true;
  loop(running);
}

void Engine::loop(bool &isRunning) {
  while (isRunning) {
    update(isRunning);
    render();
  }
}

void Engine::update(bool &isRunning) {
  
  auto &inputSystem = systemManager.getSystem<InputSystem>();
  inputSystem.update(&isRunning, systemManager);
  
  auto &timeSystem = systemManager.getSystem<TimeSystem>();
  timeSystem.update();

  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  cameraSystem.update(timeSystem.getDeltaTime(), systemManager);
  
  // Apply wireframe toggle
  auto &renderer = systemManager.getSystem<RenderSystem>().getRenderer();
  auto &state = systemManager.getSystem<StateSystem>();
  renderer.setWireframe(state.isToggled(Toggle::Wireframe));

  // Particle simulation (update phase, not render)
  auto &particleSystem = systemManager.getSystem<ParticleSystem>();
  particleSystem.update(timeSystem.getDeltaTime(), componentManager);

  // Collision detection
  auto &collisionSystem = systemManager.getSystem<CollisionSystem>();
  collisionSystem.update(componentManager);

  if (m_app) {
    m_app->update(*this, timeSystem.getDeltaTime());
  }
}

void Engine::render() {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &renderer = renderSystem.getRenderer();
  auto &uiSystem = systemManager.getSystem<UISystem>();
  auto &state = systemManager.getSystem<StateSystem>();

  renderer.beginFrame();

  uiSystem.beginFrame();

  renderSystem.renderPipeline(systemManager, entityManager, componentManager);

  if (state.isToggled(Toggle::ShowUI)) {
    uiSystem.render(entityManager, systemManager, componentManager);
  }
  uiSystem.endFrame();

  renderer.endFrame();
}

void Engine::createCameraEntity(glm::vec3 position, float yaw, float pitch, float fov) {
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  sceneSystem.createCameraEntity(position, yaw, pitch, fov);
}

Entity Engine::createModelEntity(const std::string &name, const std::string &modelPath, glm::vec3 position,
                                 glm::vec3 rotation, glm::vec3 scale) {
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  return sceneSystem.createModelEntity(name, EngineConfig::resolvePath(modelPath.c_str()), position, rotation, scale);
}

void Engine::createLightEntity(const std::string &name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                               LightType type, float intensity, float cutOff, float outerCutOff) {
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  sceneSystem.createLightEntity(name, position, direction, color, type, intensity, cutOff, outerCutOff);
}

void Engine::setState(Toggle toggle, bool value) {
  auto &stateSystem = systemManager.getSystem<StateSystem>();
  stateSystem.setToggle(toggle, value);
}

void Engine::setEmission(Entity entity, glm::vec3 color, float strength) {
  auto *model = componentManager.tryGet<Model>(entity);
  if (!model)
    return;
  systemManager.getSystem<ResourceSystem>().setEmission(*model, color, strength);
}

void Engine::setTexture(Entity entity, const std::string &path, TextureSlot slot, int submesh) {
  auto *model = componentManager.tryGet<Model>(entity);
  if (!model)
    return;
  auto &rs = systemManager.getSystem<ResourceSystem>();
  GLuint tex = rs.loadTexture(EngineConfig::resolvePath(path.c_str()));
  if (submesh < 0) {
    rs.setTexture(*model, slot, tex);
  } else if (submesh < static_cast<int>(model->materialHandles.size())) {
    rs.setMaterialTexture(model->materialHandles[submesh], model->materialHandles, slot, tex);
  }
}

void Engine::setShininess(Entity entity, float shininess, int submesh) {
  auto *model = componentManager.tryGet<Model>(entity);
  if (!model)
    return;
  auto &rs = systemManager.getSystem<ResourceSystem>();
  if (submesh < 0) {
    rs.setShininess(*model, shininess);
  } else if (submesh < static_cast<int>(model->materialHandles.size())) {
    rs.setMaterialShininess(model->materialHandles[submesh], model->materialHandles, shininess);
  }
}

Entity Engine::createEntity() { return entityManager.createEntity(); }

SystemManager &Engine::getSystemManager() { return systemManager; }
ComponentManager &Engine::getComponentManager() { return componentManager; }
EntityManager &Engine::getEntityManager() { return entityManager; }