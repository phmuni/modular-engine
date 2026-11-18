// Core engine class managing the main loop, system registration, and resource loading.

#include "foundation/core/engine.h"

#include "components/collision.h"
#include "components/name.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/resources/material.h"
#include "systems/cameraSystem.h"
#include "systems/collisionSystem.h"
#include "systems/inputSystem.h"
#include "systems/lightSystem.h"
#include "systems/particleSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSystem.h"
#include "systems/timeSystem.h"
#include "systems/transformSystem.h"
#include "systems/uiSystem.h"
#include "systems/windowSystem.h"
#include <algorithm>

Engine::Engine()
    : m_screenWidth(EngineConfig::DEFAULT_SCREEN_WIDTH), m_screenHeight(EngineConfig::DEFAULT_SCREEN_HEIGHT) {}

Engine::~Engine() { shutdown(); }

Engine::EntityBuilder::EntityBuilder(Engine &engine, std::string_view name) : m_engine(engine), m_name(name) {}

Engine::EntityBuilder &Engine::EntityBuilder::withModel(std::string_view modelPath, glm::vec3 position,
                                                        glm::vec3 rotation, glm::vec3 scale, uint32_t shaderHandle) {
  m_model = ModelConfig{std::string(modelPath), position, rotation, scale, shaderHandle};
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withCollision(const glm::vec3 &scale, bool isStatic) {
  m_actions.emplace_back([&engine = m_engine, scale, isStatic](Entity entity) {
    auto &col = engine.addComponent<Collision>(entity);
    col.min = -scale * 0.5f;
    col.max = scale * 0.5f;
    col.isStatic = isStatic;
  });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withEmission(const glm::vec3 &color, float strength) {
  m_actions.emplace_back(
      [&engine = m_engine, color, strength](Entity entity) { engine.setEmission(entity, color, strength); });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withSolidColor(const glm::vec3 &color) {
  m_actions.emplace_back([&engine = m_engine, color](Entity entity) { engine.setSolidColor(entity, color); });
  return *this;
}

Entity Engine::EntityBuilder::build() {
  if (!m_entity) {
    if (m_model) {
      const auto &model = *m_model;
      m_entity = m_engine.createModelEntity(m_name, model.path, model.position, model.rotation, model.scale,
                                            model.shaderHandle);
    } else {
      m_entity = m_engine.createEntity();
      m_engine.addComponent<Name>(*m_entity, m_name);
    }
  }

  for (auto &action : m_actions)
    action(*m_entity);
  m_actions.clear();

  return *m_entity;
}

Engine::EntityFactory::EntityFactory(Engine &engine) : m_engine(engine) {}

Engine::EntityBuilder Engine::EntityFactory::create(std::string_view name) { return EntityBuilder(m_engine, name); }

bool Engine::initialize() {
  registerSystems();

  Logger::setLevel(Logger::Level::Info);

  return loadResources();
}

void Engine::run(App &app) {
  m_app = &app;
  try {
    m_app->setup(*this);
    bool running = true;
    loop(running);
  } catch (const std::exception &ex) {
    LOG_E("[Engine] Uncaught exception in Engine::run: %s", ex.what());
    shutdown();
    return;
  } catch (...) {
    LOG_E("[Engine] Uncaught unknown exception in Engine::run");
    shutdown();
    return;
  }
}

void Engine::shutdown() {
  if (m_shutdown)
    return;
  m_shutdown = true;

  // Attempt to clean up renderer and SDL
  auto &renderSystem = m_systemManager.getSystem<RenderSystem>();
  renderSystem.getRenderer().shutdown();
  SDL_Quit();
}

void Engine::loop(bool &isRunning) {
  while (isRunning) {
    update(isRunning);
    render();
    if (m_benchmark)
      recordBenchmarkFrame();
  }
}

void Engine::runBenchmark(float seconds) {
  m_benchmark = Benchmark{};
  m_benchmark->duration = seconds;
  setState(Toggle::VSync, false);
}

void Engine::recordBenchmarkFrame() {
  auto &bench = *m_benchmark;
  uint64_t now = SDL_GetTicksNS();
  if (bench.lastFrame == 0) {
    bench.lastFrame = now;
    return;
  }

  double frameMs = static_cast<double>(now - bench.lastFrame) / 1e6;
  bench.lastFrame = now;
  bench.elapsed += frameMs / 1000.0;

  if (bench.elapsed < bench.warmup)
    return;
  bench.frameMs.push_back(frameMs);
  if (bench.elapsed < bench.warmup + bench.duration)
    return;

  std::vector<double> sorted = bench.frameMs;
  std::sort(sorted.begin(), sorted.end());
  auto percentile = [&](double p) { return sorted[static_cast<size_t>(p * (sorted.size() - 1))]; };
  double total = 0.0;
  for (double ms : sorted)
    total += ms;
  double average = total / static_cast<double>(sorted.size());

  LOG_I("[Benchmark] %zu frames | avg %.3f ms (%.0f FPS) | p50 %.3f | p95 %.3f | p99 %.3f | max %.3f ms", sorted.size(),
        average, 1000.0 / average, percentile(0.50), percentile(0.95), percentile(0.99), sorted.back());

  m_benchmark.reset();
  m_quitRequested = true;
}

void Engine::update(bool &isRunning) {
  m_frameStartNs = SDL_GetTicksNS();

  auto &inputSystem = m_systemManager.getSystem<InputSystem>();
  inputSystem.update(&isRunning, m_systemManager);

  auto &state = m_systemManager.getSystem<StateSystem>();
  auto &timeSystem = m_systemManager.getSystem<TimeSystem>();
  timeSystem.setPaused(state.isToggled(Toggle::Paused));
  timeSystem.update();

  // The camera runs on real time, so it can still fly around a paused scene
  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  cameraSystem.update(timeSystem.getUnscaledDeltaTime(), m_systemManager);

  // Apply wireframe and vsync toggles
  auto &renderer = m_systemManager.getSystem<RenderSystem>().getRenderer();
  renderer.setWireframe(state.isToggled(Toggle::Wireframe));
  m_systemManager.getSystem<WindowSystem>().setVSync(state.isToggled(Toggle::VSync));

  // Particle simulation (update phase, not render)
  auto &particleSystem = m_systemManager.getSystem<ParticleSystem>();
  particleSystem.update(timeSystem.getDeltaTime());

  // Collision detection
  auto &collisionSystem = m_systemManager.getSystem<CollisionSystem>();
  collisionSystem.update(m_componentManager);

  // UI/Cursor show detection
  auto &uiSystem = m_systemManager.getSystem<UISystem>();
  uiSystem.update(m_systemManager);

  if (m_app) {
    m_app->update(*this, timeSystem.getDeltaTime());
  }

  // After the app had its chance to destroy entities this frame
  m_systemManager.getSystem<ResourceSystem>().collectUnusedMaterials();

  bool screenshotKey = inputSystem.isKeyPressed(EngineConfig::SCREENSHOT_KEY);
  if (screenshotKey && !m_screenshotKeyDown)
    captureScreenshot(EngineConfig::resolvePath(EngineConfig::SCREENSHOT_PATH));
  m_screenshotKeyDown = screenshotKey;

  if (m_screenshot)
    m_screenshot->delay -= timeSystem.getUnscaledDeltaTime();

  if (m_quitRequested)
    isRunning = false;
}

void Engine::render() {
  auto &renderSystem = m_systemManager.getSystem<RenderSystem>();
  auto &renderer = renderSystem.getRenderer();
  auto &uiSystem = m_systemManager.getSystem<UISystem>();

  renderer.beginFrame();

  uiSystem.beginFrame();

  renderSystem.renderPipeline(m_systemManager, m_componentManager);

  uiSystem.render(*this, m_entityManager, m_systemManager, m_componentManager);

  uiSystem.endFrame();

  // Everything this frame did before the swap, which may wait on vsync
  float workSeconds = static_cast<float>(static_cast<double>(SDL_GetTicksNS() - m_frameStartNs) / 1e9);
  uiSystem.recordFrame(m_systemManager.getSystem<TimeSystem>().getUnscaledDeltaTime(), workSeconds);

  if (m_screenshot && m_screenshot->delay <= 0.0f) {
    renderer.saveScreenshot(m_screenshot->path);
    m_quitRequested = m_screenshot->quitAfter;
    m_screenshot.reset();
  }

  renderer.endFrame();
}

Entity Engine::createEntity() { return m_entityManager.createEntity(); }

Engine::EntityFactory Engine::entities() { return EntityFactory(*this); }

void Engine::createCameraEntity(std::string_view name, glm::vec3 position, float yaw, float pitch, float fov,
                                bool isActive) {
  m_systemManager.getSystem<SceneSystem>().createCameraEntity(std::string(name), position, yaw, pitch, fov, isActive);
}

Entity Engine::createModelEntity(std::string_view name, std::string_view modelPath, glm::vec3 position,
                                 glm::vec3 rotation, glm::vec3 scale, uint32_t shaderHandle) {
  uint32_t effectiveShader = shaderHandle == 0 ? m_baseShaderHandle : shaderHandle;
  return m_systemManager.getSystem<SceneSystem>().createModelEntity(
      std::string(name), EngineConfig::resolvePath(std::string(modelPath)), effectiveShader, position, rotation, scale);
}

Entity Engine::createLightEntity(std::string_view name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                                 LightType type, float intensity, float cutOff, float outerCutOff) {
  return m_systemManager.getSystem<SceneSystem>().createLightEntity(std::string(name), position, direction, color, type,
                                                                    intensity, cutOff, outerCutOff);
}

void Engine::setState(Toggle toggle, bool value) {
  auto &stateSystem = m_systemManager.getSystem<StateSystem>();
  stateSystem.setToggle(toggle, value);
}

void Engine::captureScreenshot(std::string path, float delaySeconds, bool quitAfter) {
  m_screenshot = ScreenshotRequest{std::move(path), delaySeconds, quitAfter};
}

void Engine::setEmission(Entity entity, glm::vec3 color, float strength, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setEmission(entity, submesh, color, strength);
}

void Engine::setTexture(Entity entity, TextureSlot slot, std::string_view path, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setTexture(entity, submesh, slot, path);
}

void Engine::setShininess(Entity entity, float shininess, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setShininess(entity, submesh, shininess);
}

void Engine::setSolidColor(Entity entity, glm::vec3 color, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setSolidColor(entity, submesh, color);
}

void Engine::setEmission(Entity entity, glm::vec3 color, float strength, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setEmission(entity, submesh, color, strength);
}

void Engine::setTexture(Entity entity, TextureSlot slot, std::string_view path, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setTexture(entity, submesh, slot, path);
}

void Engine::setShininess(Entity entity, float shininess, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setShininess(entity, submesh, shininess);
}

void Engine::setSolidColor(Entity entity, glm::vec3 color, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setSolidColor(entity, submesh, color);
}

SystemManager &Engine::getSystemManager() { return m_systemManager; }
ComponentManager &Engine::getComponentManager() { return m_componentManager; }
EntityManager &Engine::getEntityManager() { return m_entityManager; }

void Engine::registerSystems() {
  m_systemManager.insert<WindowSystem>(m_screenWidth, m_screenHeight);
  m_systemManager.insert<StateSystem>();
  m_systemManager.insert<InputSystem>();
  m_systemManager.insert<TimeSystem>();
  m_systemManager.insert<ResourceSystem>(m_componentManager);
  m_systemManager.insert<RenderSystem>();
  m_systemManager.insert<TransformSystem>();
  m_systemManager.insert<CameraSystem>(m_componentManager, m_systemManager.getSystem<InputSystem>());
  m_systemManager.insert<LightSystem>();
  m_systemManager.insert<SceneSystem>(m_entityManager, m_componentManager, m_systemManager);
  m_systemManager.insert<ParticleSystem>(m_componentManager);
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
  uint32_t particleShader =
      resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX_PARTICLE),
                                EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT_PARTICLE));
  if (!baseShader || !shadowShader || !particleShader) {
    LOG_E("[Engine] Failed to load shaders.");
    return false;
  }

  m_baseShaderHandle = baseShader;
  resourceSystem.getMaterial(0).setShaderHandle(baseShader);
  m_systemManager.getSystem<RenderSystem>().setShadowShaderHandle(shadowShader);

  auto &particleSystem = m_systemManager.getSystem<ParticleSystem>();
  particleSystem.setShaderHandle(particleShader);

  return true;
}