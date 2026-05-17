
// Core engine class managing the main loop, system registration, and resource loading.

#include "foundation/core/engine.h"

#include "foundation/core/config.h"
#include "systems/cameraSystem.h"
#include "systems/collisionSystem.h"
#include "systems/inputSystem.h"
#include "systems/lightSystem.h"
#include "systems/particleSystem.h"
#include "systems/renderSystem.h"
#include "systems/sceneSystem.h"
#include "systems/timeSystem.h"
#include "systems/transformSystem.h"
#include "systems/uiSystem.h"
#include "systems/windowSystem.h"

Engine::Engine()
    : m_screenWidth(EngineConfig::DEFAULT_SCREEN_WIDTH), m_screenHeight(EngineConfig::DEFAULT_SCREEN_HEIGHT) {}

Engine::~Engine() {
  auto &renderSystem = m_systemManager.getSystem<RenderSystem>();
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
  m_systemManager.insert<WindowSystem>(m_screenWidth, m_screenHeight);
  m_systemManager.insert<StateSystem>();
  m_systemManager.insert<InputSystem>();
  m_systemManager.insert<TimeSystem>();
  m_systemManager.insert<ResourceSystem>();
  m_systemManager.insert<RenderSystem>();
  m_systemManager.insert<TransformSystem>();
  m_systemManager.insert<CameraSystem>(m_componentManager, m_systemManager.getSystem<InputSystem>());
  m_systemManager.insert<LightSystem>();
  m_systemManager.insert<SceneSystem>(m_entityManager, m_componentManager, m_systemManager);
  m_systemManager.insert<ParticleSystem>();
  m_systemManager.insert<CollisionSystem>();
  m_systemManager.insert<UISystem>(m_systemManager.getSystem<WindowSystem>().getWindow(),
                                   m_systemManager.getSystem<WindowSystem>().getContext());
}

bool Engine::loadResources() {
  auto &renderer = m_systemManager.getSystem<RenderSystem>().getRenderer();
  auto &windowSystem = m_systemManager.getSystem<WindowSystem>();
  auto &resourceSystem = m_systemManager.getSystem<ResourceSystem>();

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

  auto &particleSystem = m_systemManager.getSystem<ParticleSystem>();
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

  auto &inputSystem = m_systemManager.getSystem<InputSystem>();
  inputSystem.update(&isRunning, m_systemManager);

  auto &timeSystem = m_systemManager.getSystem<TimeSystem>();
  timeSystem.update();

  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  cameraSystem.update(timeSystem.getDeltaTime(), m_systemManager);

  // Apply wireframe toggle
  auto &renderer = m_systemManager.getSystem<RenderSystem>().getRenderer();
  auto &state = m_systemManager.getSystem<StateSystem>();
  renderer.setWireframe(state.isToggled(Toggle::Wireframe));

  // Particle simulation (update phase, not render)
  auto &particleSystem = m_systemManager.getSystem<ParticleSystem>();
  particleSystem.update(timeSystem.getDeltaTime(), m_componentManager);

  // Collision detection
  auto &collisionSystem = m_systemManager.getSystem<CollisionSystem>();
  collisionSystem.update(m_componentManager);

  if (m_app) {
    m_app->update(*this, timeSystem.getDeltaTime());
  }
}

void Engine::render() {
  auto &renderSystem = m_systemManager.getSystem<RenderSystem>();
  auto &renderer = renderSystem.getRenderer();
  auto &uiSystem = m_systemManager.getSystem<UISystem>();
  auto &state = m_systemManager.getSystem<StateSystem>();

  renderer.beginFrame();

  uiSystem.beginFrame();

  renderSystem.renderPipeline(m_systemManager, m_entityManager, m_componentManager);

  if (state.isToggled(Toggle::ShowUI)) {
    uiSystem.render(m_entityManager, m_systemManager, m_componentManager);
  }
  uiSystem.endFrame();

  renderer.endFrame();
}

void Engine::createCameraEntity(std::string_view name, glm::vec3 position, float yaw, float pitch, float fov,
                                bool isActive) {
  m_systemManager.getSystem<SceneSystem>().createCameraEntity(std::string(name), position, yaw, pitch, fov, isActive);
}

Entity Engine::createModelEntity(std::string_view name, std::string_view modelPath, glm::vec3 position,
                                 glm::vec3 rotation, glm::vec3 scale) {
  return m_systemManager.getSystem<SceneSystem>().createModelEntity(
      std::string(name), EngineConfig::resolvePath(std::string(modelPath)), position, rotation, scale);
}

void Engine::createLightEntity(std::string_view name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                               LightType type, float intensity, float cutOff, float outerCutOff) {
  m_systemManager.getSystem<SceneSystem>().createLightEntity(std::string(name), position, direction, color, type,
                                                             intensity, cutOff, outerCutOff);
}

void Engine::setState(Toggle toggle, bool value) {
  auto &stateSystem = m_systemManager.getSystem<StateSystem>();
  stateSystem.setToggle(toggle, value);
}

void Engine::setEmission(Entity entity, glm::vec3 color, float strength) {
  auto *model = m_componentManager.getOrNil<Model>(entity);
  if (!model)
    return;
  m_systemManager.getSystem<ResourceSystem>().setEmission(*model, color, strength);
}

void Engine::setTexture(Entity entity, std::string_view path, TextureSlot slot, int submesh) {
  auto *model = m_componentManager.getOrNil<Model>(entity);
  if (!model)
    return;
  auto &rs = m_systemManager.getSystem<ResourceSystem>();
  GLuint tex = rs.loadTexture(EngineConfig::resolvePath(path));
  if (submesh < 0) {
    rs.setTexture(*model, slot, tex);
  } else if (submesh < static_cast<int>(model->materialHandles.size())) {
    rs.setMaterialTexture(model->materialHandles[submesh], model->materialHandles, slot, tex);
  }
}

void Engine::setShininess(Entity entity, float shininess, int submesh) {
  auto *model = m_componentManager.getOrNil<Model>(entity);
  if (!model)
    return;
  auto &rs = m_systemManager.getSystem<ResourceSystem>();
  if (submesh < 0) {
    rs.setShininess(*model, shininess);
  } else if (submesh < static_cast<int>(model->materialHandles.size())) {
    rs.setMaterialShininess(model->materialHandles[submesh], model->materialHandles, shininess);
  }
}

Entity Engine::createEntity() { return m_entityManager.createEntity(); }

SystemManager &Engine::getSystemManager() { return m_systemManager; }
ComponentManager &Engine::getComponentManager() { return m_componentManager; }
EntityManager &Engine::getEntityManager() { return m_entityManager; }