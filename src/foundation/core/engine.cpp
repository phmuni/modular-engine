// Core engine class managing the main loop, system registration, and resource loading.

#include "foundation/core/engine.h"

#include "components/collision.h"
#include "components/hidden.h"
#include "components/camera.h"
#include "components/cameraRig.h"
#include "components/model.h"
#include "components/name.h"
#include "components/parent.h"
#include "components/tags.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "foundation/core/random.h"
#include "foundation/core/typeName.h"
#include <cstdint>
#include "rendering/resources/material.h"
#include "rendering/resources/textureProcessing.h"
#include "systems/animationSystem.h"
#include "systems/scriptSystem.h"
#include "systems/audioSystem.h"
#include "systems/cameraSystem.h"
#include "systems/collisionSystem.h"
#include "systems/gameUISystem.h"
#include "systems/inputSystem.h"
#include "systems/lightSystem.h"
#include "systems/modelQueries.h"
#include "systems/navigationSystem.h"
#include "systems/particleSystem.h"
#include "systems/pauseMenu.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/saveData.h"
#include "systems/sceneSerializer.h"
#include "systems/sceneSystem.h"
#include "systems/timeSystem.h"
#include "systems/transformSystem.h"
#include "systems/editorUISystem.h"
#include "components/particleEmitter.h"
#include "systems/windowSystem.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <limits>

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

Engine::EntityBuilder &Engine::EntityBuilder::withModelCollision(float fit, bool isStatic) {
  m_actions.emplace_back([&engine = m_engine, fit, isStatic](Entity entity) {
    engine.fitCollisionToModel(entity, fit, isStatic);
  });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withEmission(const glm::vec3 &color, float strength) {
  m_actions.emplace_back(
      [&engine = m_engine, color, strength](Entity entity) { engine.setEmission(entity, color, strength); });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withLight(const Light &light) {
  m_actions.emplace_back([&engine = m_engine, light](Entity entity) {
    engine.addComponent<Light>(entity, light);
    engine.m_systemManager.getSystem<LightSystem>().createLight(entity);
  });
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

  // Last, once it has its Transform: what it was given becomes its place relative to the parent
  if (m_parent != -1) {
    if (const Transform *given = m_engine.tryGet<Transform>(*m_entity)) {
      const Transform local = *given;
      if (m_engine.setParent(*m_entity, m_parent))
        m_engine.setLocalTransform(*m_entity, local);
    }
    m_parent = -1;
  }

  return *m_entity;
}

Engine::EntityFactory::EntityFactory(Engine &engine) : m_engine(engine) {}

Engine::EntityBuilder Engine::EntityFactory::create(std::string_view name) { return EntityBuilder(m_engine, name); }

bool Engine::initialize() {
  Logger::setLevel(Logger::Level::Info);

  registerSystems();

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
    reportFatalError(ex.what());
    shutdown();
    return;
  } catch (...) {
    LOG_E("[Engine] Uncaught unknown exception in Engine::run");
    reportFatalError("Unknown error");
    shutdown();
    return;
  }
}

void Engine::reportFatalError(const std::string &message) {
  // Unattended runs (screenshots, benchmarks, scene saves) must not wait on a dialog
  if (m_screenshot || m_benchmark || m_sceneSave)
    return;
  std::string text = message + "\n\nThe engine will close. Details are in the log.";
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Modular Engine", text.c_str(),
                           m_systemManager.getSystem<WindowSystem>().getWindow());
}

void Engine::shutdown() {
  if (m_shutdown)
    return;
  m_shutdown = true;

  m_systemManager.getSystem<SaveData>().writeIfChanged();
  // Attempt to clean up renderer and SDL; the game UI frees its GPU resources first, while the context is alive
  m_systemManager.getSystem<GameUISystem>().shutdown();
  auto &renderSystem = m_systemManager.getSystem<RenderSystem>();
  renderSystem.getRenderer().shutdown();
  SDL_Quit();
}

void Engine::loop(bool &isRunning) {
  // The first frame measures from here, not from engine start-up (which includes the app's setup)
  m_systemManager.getSystem<TimeSystem>().begin();
  while (isRunning) {
    update(isRunning);
    render();
    if (m_benchmark)
      recordBenchmarkFrame();
  }
}

void Engine::setDeterministic() {
  m_deterministic = true;
  // The player's save file stays out of it, both ways
  m_systemManager.getSystem<SaveData>().setInMemory(true);
  Random::seed(1);
  m_systemManager.getSystem<TimeSystem>().setFixedStep(1.0f / 60.0f);
  // Whole textures each frame: streaming would depend on when background reads finish
  m_systemManager.getSystem<ResourceSystem>().setTextureUploadBudget(SIZE_MAX);
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

  // Play, Stop and freezing, from the editor's last frame
  applyPlayModeRequest();
  applyHistoryRequest();

  // Before the pause state is read below; while it is open the game sees no input
  auto &pauseMenu = m_systemManager.getSystem<PauseMenu>();
  pauseMenu.update(inputSystem.wasEscapePressed());
  inputSystem.setGameInputBlocked(pauseMenu.isOpen());

  auto &state = m_systemManager.getSystem<StateSystem>();
  // Upload models whose background reads finished
  m_systemManager.getSystem<ResourceSystem>().update();

  auto &timeSystem = m_systemManager.getSystem<TimeSystem>();
  timeSystem.setPaused(state.isToggled(Toggle::Paused) || m_playMode == PlayMode::Editing);
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

  // UI/Cursor show detection
  auto &editorUI = m_systemManager.getSystem<EditorUISystem>();
  editorUI.update(m_systemManager);

  if (m_playMode != PlayMode::Editing)
    m_time.tick(timeSystem.getDeltaTime()); // the game clock and timers, before the app
  if (m_app && m_playMode != PlayMode::Editing) {
    m_app->update(*this, timeSystem.getDeltaTime());
  }
  // Lua scripts, after the app (frozen while editing, like it)
  if (m_playMode != PlayMode::Editing)
    m_systemManager.getSystem<ScriptSystem>().update(timeSystem.getDeltaTime());

  // Scene switches wait until the app is done with this frame's entities
  applyPendingScene();

  // Bodies fall, collide and push; then children follow where their parents went this frame
  m_systemManager.getSystem<PhysicsSystem>().update(timeSystem.getDeltaTime());
  m_systemManager.getSystem<NavigationSystem>().update();
  m_systemManager.getSystem<TransformSystem>().updateHierarchy(m_componentManager);
  // Skeletons posed from their clips (time stands still while paused or editing)
  m_systemManager.getSystem<AnimationSystem>().update(timeSystem.getDeltaTime());
  // The camera rig last of the movement: it trails where the target ended up this frame
  cameraSystem.updateRigs(timeSystem.getDeltaTime(), m_systemManager);

  if (m_sceneSave) {
    m_sceneSave->delay -= timeSystem.getUnscaledDeltaTime();
    if (m_sceneSave->delay <= 0.0f) {
      saveScene(m_sceneSave->path);
      m_quitRequested = m_quitRequested || m_sceneSave->quitAfter;
      m_sceneSave.reset();
    }
  }

  // After the app had its chance to destroy entities this frame
  m_systemManager.getSystem<ResourceSystem>().collectUnusedMaterials();
  m_systemManager.getSystem<AudioSystem>().update(m_systemManager, state.isToggled(Toggle::Paused),
                                                  timeSystem.getTimeScale());
  // After the app, so data models it changed this frame are shown this frame
  m_systemManager.getSystem<GameUISystem>().update(state.isToggled(Toggle::UIDebugger),
                                                   timeSystem.getUnscaledDeltaTime());

  bool screenshotKey = inputSystem.isKeyDown(EngineConfig::SCREENSHOT_KEY);
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
  auto &editorUI = m_systemManager.getSystem<EditorUISystem>();

  renderer.beginFrame();

  editorUI.beginFrame();

  renderSystem.renderPipeline(m_systemManager, m_componentManager);

  // Game UI over the scene, debug panels over the game UI
  m_systemManager.getSystem<GameUISystem>().render();

  editorUI.render(*this, m_entityManager, m_systemManager, m_componentManager);

  editorUI.endFrame();

  // Everything this frame did before the swap, which may wait on vsync
  float workSeconds = static_cast<float>(static_cast<double>(SDL_GetTicksNS() - m_frameStartNs) / 1e9);
  editorUI.recordFrame(m_systemManager.getSystem<TimeSystem>().getUnscaledDeltaTime(), workSeconds);

  if (m_screenshot && m_screenshot->delay <= 0.0f) {
    renderer.saveScreenshot(m_screenshot->path);
    m_quitRequested = m_screenshot->quitAfter;
    m_screenshot.reset();
  }

  renderer.endFrame();
}

Entity Engine::createEntity() { return m_entityManager.createEntity(); }

Engine::EntityFactory Engine::entities() { return EntityFactory(*this); }

void Engine::destroyEntity(Entity entity) { m_systemManager.getSystem<SceneSystem>().destroyEntity(entity); }

bool Engine::isAlive(Entity entity) const { return m_entityManager.isAlive(entity); }

void Engine::throwMissingComponent(Entity entity, const std::type_info &type) const {
  std::string message;
  if (!m_entityManager.isAlive(entity))
    message = "entity " + std::to_string(entity) + " no longer exists (destroyed, or its scene was replaced), so it " +
              "has no " + typeName(type);
  else
    message = "entity " + std::to_string(entity) + " has no " + typeName(type) + " component";
  LOG_E("[Engine] get: %s", message.c_str());
  throw std::runtime_error(message);
}

Camera *Engine::getActiveCamera() {
  Entity camera = m_systemManager.getSystem<CameraSystem>().getActiveCamera();
  return camera == -1 ? nullptr : m_componentManager.tryGet<Camera>(camera);
}

InputSystem &Engine::input() { return m_systemManager.getSystem<InputSystem>(); }

PhysicsSystem &Engine::physics() { return m_systemManager.getSystem<PhysicsSystem>(); }

NavigationSystem &Engine::navigation() { return m_systemManager.getSystem<NavigationSystem>(); }

ScriptSystem &Engine::scripts() { return m_systemManager.getSystem<ScriptSystem>(); }

void Engine::playAnimation(Entity entity, std::string_view clip, float fadeSeconds, bool loop) {
  Animator *animator = tryGet<Animator>(entity);
  if (!animator) {
    animator = &addComponent<Animator>(entity);
    fadeSeconds = 0.0f; // nothing to fade from
  }
  const Model *model = tryGet<Model>(entity);
  const MeshAnimation *animation =
      model ? m_systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle).getAnimation() : nullptr;
  if (animation && !clip.empty() && animation->findClip(clip) < 0)
    LOG_W("[Engine] playAnimation: entity %d's model has no clip \"%.*s\"", entity, static_cast<int>(clip.size()),
          clip.data());
  const bool same = animator->clip == clip && animator->playing;
  animator->loop = loop;
  animator->playing = true;
  if (same)
    return; // already playing it: keep going rather than restart
  if (fadeSeconds > 0.0f) {
    animator->fadeFrom = animator->clip.empty() && animation && !animation->clips.empty() ? animation->clips[0].name
                                                                                         : animator->clip;
    animator->fadeFromTime = animator->time;
    animator->fadeElapsed = 0.0f;
    animator->fadeDuration = fadeSeconds;
  } else {
    animator->fadeFrom.clear();
  }
  animator->clip = std::string(clip);
  animator->time = 0.0f;
}

std::vector<std::string> Engine::getAnimationClips(Entity entity) {
  std::vector<std::string> names;
  const Model *model = tryGet<Model>(entity);
  if (const MeshAnimation *animation =
          model ? m_systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle).getAnimation() : nullptr)
    for (const AnimationClip &clip : animation->clips)
      names.push_back(clip.name);
  return names;
}

std::optional<RayHit> Engine::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                      const std::function<bool(Entity)> &accept) {
  return m_systemManager.getSystem<CollisionSystem>().raycast(origin, direction, maxDistance, m_componentManager,
                                                             accept);
}

std::optional<Engine::Ray> Engine::screenRay(glm::vec2 windowPosition) {
  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? tryGet<Camera>(cameraEntity) : nullptr;
  int width = 0, height = 0;
  SDL_GetWindowSize(m_systemManager.getSystem<WindowSystem>().getWindow(), &width, &height);
  if (!camera || width <= 0 || height <= 0)
    return std::nullopt;

  // Window to normalized device coordinates (y up), then back through the camera onto the near and far planes
  const glm::vec2 ndc(windowPosition.x / width * 2.0f - 1.0f, 1.0f - windowPosition.y / height * 2.0f);
  const glm::mat4 inverse =
      glm::inverse(cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera));
  glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f);
  glm::vec4 farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
  nearPoint /= nearPoint.w;
  farPoint /= farPoint.w;
  return Ray{glm::vec3(nearPoint), glm::normalize(glm::vec3(farPoint - nearPoint))};
}

std::optional<RayHit> Engine::raycastModels(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                            const std::function<bool(Entity)> &accept) {
  const ModelQueries queries(m_componentManager, m_systemManager.getSystem<ResourceSystem>(),
                             m_systemManager.getSystem<TransformSystem>());
  if (auto hit = queries.raycast(origin, direction, maxDistance, accept))
    return RayHit{hit->entity, hit->distance, hit->point, hit->normal};
  return std::nullopt;
}

std::optional<RayHit> Engine::pickModel(glm::vec2 windowPosition) {
  if (auto ray = screenRay(windowPosition))
    return raycastModels(ray->origin, ray->direction);
  return std::nullopt;
}

void Engine::fitCollisionToModel(Entity entity, float fit, bool isStatic) {
  const Model *model = tryGet<Model>(entity);
  const Transform *transform = tryGet<Transform>(entity);
  if (!model || !transform) {
    LOG_W("[Engine] fitCollisionToModel: entity %d needs a Model and a Transform", entity);
    return;
  }
  const Mesh &mesh = m_systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle);
  Transform unmoved = *transform;
  unmoved.position = glm::vec3(0.0f); // the box is relative to the Transform's position
  const glm::mat4 matrix = m_systemManager.getSystem<TransformSystem>().calculateModelMatrix(unmoved);

  // The eight corners of the model's box, rotated and scaled, then boxed again along the world axes
  const glm::vec3 low = mesh.getBoundsMin(), high = mesh.getBoundsMax();
  glm::vec3 boxMin(std::numeric_limits<float>::max()), boxMax(-std::numeric_limits<float>::max());
  for (int corner = 0; corner < 8; ++corner) {
    const glm::vec3 local(corner & 1 ? high.x : low.x, corner & 2 ? high.y : low.y, corner & 4 ? high.z : low.z);
    const glm::vec3 world = glm::vec3(matrix * glm::vec4(local, 1.0f));
    boxMin = glm::min(boxMin, world);
    boxMax = glm::max(boxMax, world);
  }
  const glm::vec3 center = (boxMin + boxMax) * 0.5f, half = (boxMax - boxMin) * 0.5f * fit;

  Collision &collision = has<Collision>(entity) ? get<Collision>(entity) : addComponent<Collision>(entity);
  collision.min = center - half;
  collision.max = center + half;
  collision.isStatic = isStatic;
}

const std::vector<CollisionEvent> &Engine::findCollisions() {
  return m_systemManager.getSystem<CollisionSystem>().findCollisions(m_componentManager);
}

bool Engine::isColliding(Entity a, Entity b) {
  return m_systemManager.getSystem<CollisionSystem>().overlaps(a, b, m_componentManager);
}

bool Engine::getState(Toggle toggle) { return m_systemManager.getSystem<StateSystem>().isToggled(toggle); }

Entity Engine::createCameraEntity(std::string_view name, glm::vec3 position, float yaw, float pitch, float fov,
                                  bool isActive) {
  return m_systemManager.getSystem<SceneSystem>().createCameraEntity(std::string(name), position, yaw, pitch, fov,
                                                                     isActive);
}

void Engine::preloadModel(std::string_view modelPath) {
  m_systemManager.getSystem<ResourceSystem>().preloadModel(EngineConfig::resolveAsset(std::string(modelPath)));
}

Entity Engine::createModelEntity(std::string_view name, std::string_view modelPath, glm::vec3 position,
                                 glm::vec3 rotation, glm::vec3 scale, uint32_t shaderHandle) {
  uint32_t effectiveShader = shaderHandle == 0 ? m_baseShaderHandle : shaderHandle;
  return m_systemManager.getSystem<SceneSystem>().createModelEntity(
      std::string(name), EngineConfig::resolveAsset(std::string(modelPath)), effectiveShader, position, rotation, scale);
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

bool Engine::saveScene(std::string_view path) {
  if (!m_systemManager.getSystem<SceneSerializer>().save(std::string(path)))
    return false;
  m_scenePath = path;
  return true;
}

SceneSerializer &Engine::getSceneSerializer() { return m_systemManager.getSystem<SceneSerializer>(); }

ReflectedComponent &Engine::addReflectedComponent(ReflectedComponent component) {
  ReflectedComponent &stored = m_reflectedComponents.emplace_back(std::move(component));
  getSceneSerializer().registerReflected(stored);
  return stored;
}

void Engine::loadScene(std::string_view path) {
  m_pendingScene = std::string(path);
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  m_pendingSceneModels = m_systemManager.getSystem<SceneSerializer>().modelPaths(*m_pendingScene);
  for (const std::string &model : m_pendingSceneModels)
    resources.preloadModel(model); // returns at once for models already loaded or being read
}

void Engine::saveSceneAfter(std::string path, float delaySeconds, bool quitAfter) {
  m_sceneSave = ScreenshotRequest{std::move(path), delaySeconds, quitAfter};
}

void Engine::applyPendingScene() {
  if (!m_pendingScene)
    return;
  // Waits while the scene's models are read in the background; ResourceSystem::update uploads each as it finishes.
  // Deterministic runs switch on the next frame and wait for the reads there, so the frame does not vary.
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  for (const std::string &model : m_pendingSceneModels)
    if (!m_deterministic && resources.isModelPending(model))
      return;
  m_pendingSceneModels.clear();
  std::string path = std::move(*m_pendingScene);
  m_pendingScene.reset();
  if (!m_systemManager.getSystem<SceneSerializer>().load(path))
    return;
  m_scenePath = path;
  m_systemManager.getSystem<NavigationSystem>().clear(); // the map was of the old level
  clearHistory();
  if (m_app)
    m_app->onSceneLoaded(*this, path);
}

void Engine::preloadSound(std::string_view path) {
  m_systemManager.getSystem<AudioSystem>().preload(std::string(path));
}

uint32_t Engine::playSound(std::string_view path, float volume, float pitch) {
  return m_systemManager.getSystem<AudioSystem>().playOneShot(std::string(path), std::nullopt, volume, pitch);
}

uint32_t Engine::playSoundAt(std::string_view path, glm::vec3 position, float volume, float pitch) {
  return m_systemManager.getSystem<AudioSystem>().playOneShot(std::string(path), position, volume, pitch);
}

void Engine::stopSound(uint32_t sound) { m_systemManager.getSystem<AudioSystem>().stopVoice(sound); }

void Engine::setSoundVolume(uint32_t sound, float volume) {
  m_systemManager.getSystem<AudioSystem>().setVoiceVolume(sound, volume);
}

bool Engine::isSoundPlaying(uint32_t sound) { return m_systemManager.getSystem<AudioSystem>().isVoicePlaying(sound); }

void Engine::setMusicVolume(float volume) { m_systemManager.getSystem<AudioSystem>().setMusicVolume(volume); }

void Engine::playMusic(std::string_view path, float volume, bool loop) {
  m_systemManager.getSystem<AudioSystem>().playMusic(std::string(path), volume, loop);
}

Rml::Context *Engine::getUIContext() { return m_systemManager.getSystem<GameUISystem>().getContext(); }

Rml::ElementDocument *Engine::loadUIDocument(std::string_view path, bool show) {
  return m_systemManager.getSystem<GameUISystem>().loadDocument(std::string(path), show);
}

bool Engine::loadUIFont(std::string_view path, bool fallback) {
  return m_systemManager.getSystem<GameUISystem>().loadFont(std::string(path), fallback);
}

void Engine::stopMusic() { m_systemManager.getSystem<AudioSystem>().stopMusic(); }

Engine::CommandLine Engine::parseCommandLine(int argc, char **argv) {
  CommandLine result;
  // An optional number after an option: a following argument that does not start with '-'
  auto number = [&](int &i, float &value) {
    if (i + 1 < argc && argv[i + 1][0] != '-')
      value = std::strtof(argv[++i], nullptr);
  };
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--screenshot" && i + 1 < argc) {
      result.screenshotPath = std::filesystem::absolute(argv[++i]).string(); // relative to where it was started
      number(i, result.screenshotDelay);
    } else if (arg == "--benchmark") {
      result.benchmarkSeconds = 10.0f;
      number(i, result.benchmarkSeconds);
    } else if (arg == "--save-scene" && i + 1 < argc) {
      result.saveScenePath = std::filesystem::absolute(argv[++i]).string();
      number(i, result.saveSceneDelay);
    } else if (arg == "--deterministic") {
      result.deterministic = true;
    } else {
      result.arguments.push_back(arg);
    }
  }
  return result;
}

const char *Engine::commandLineHelp() {
  return "  --screenshot <file.bmp> [seconds]   Save a frame after that long (3 s by default) and quit\n"
         "  --benchmark [seconds]               Log frame-time statistics (10 s by default) and quit\n"
         "  --save-scene <file.json> [seconds]  Save what the game built (after 0 s by default) and quit\n"
         "  --deterministic                     Fixed 1/60 s frames and seeded randomness, for repeatable runs\n";
}

void Engine::applyCommandLine(const CommandLine &commandLine) {
  // Before the app exists, so its setup already runs on fixed frames and seeded randomness
  if (commandLine.deterministic)
    setDeterministic();
  if (!commandLine.screenshotPath.empty())
    captureScreenshot(commandLine.screenshotPath, commandLine.screenshotDelay, true);
  if (!commandLine.saveScenePath.empty())
    saveSceneAfter(commandLine.saveScenePath, commandLine.saveSceneDelay, true);
  if (commandLine.benchmarkSeconds > 0.0f)
    runBenchmark(commandLine.benchmarkSeconds);
}

bool Engine::setParent(Entity child, Entity parent) {
  return m_systemManager.getSystem<TransformSystem>().setParent(m_componentManager, child, parent);
}

Entity Engine::getParent(Entity entity) {
  return m_systemManager.getSystem<TransformSystem>().getParent(m_componentManager, entity);
}

std::vector<Entity> Engine::getChildren(Entity entity) {
  return m_systemManager.getSystem<TransformSystem>().getChildren(m_componentManager, entity);
}

void Engine::setLocalTransform(Entity entity, const Transform &local) {
  m_systemManager.getSystem<TransformSystem>().setLocalTransform(m_componentManager, entity, local);
}

Engine::SceneSnapshot Engine::captureScene() {
  SceneSnapshot snapshot;
  snapshot.components = m_componentManager.snapshot();
  snapshot.entities = m_entityManager;
  snapshot.activeCamera = m_systemManager.getSystem<CameraSystem>().getActiveCamera();
  m_componentManager.forEachComponent<Model>([&](Entity, Model &model) {
    snapshot.materials.insert(snapshot.materials.end(), model.materialHandles.begin(), model.materialHandles.end());
  });
  return snapshot;
}

void Engine::restoreScene(SceneSnapshot snapshot) {
  m_componentManager.restore(std::move(snapshot.components));
  m_entityManager.restore(snapshot.entities);
  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  if (snapshot.activeCamera != -1 && m_entityManager.isAlive(snapshot.activeCamera))
    cameraSystem.setActiveCamera(snapshot.activeCamera);
  else
    cameraSystem.removeActiveCamera();
  m_systemManager.getSystem<LightSystem>().rebuild(m_componentManager);
  m_systemManager.getSystem<RenderSystem>().resetCaches();
  m_systemManager.getSystem<PhysicsSystem>().reset();
  // The walls may be back or gone: the navigation map is built again (with its last settings) on the next update
  m_systemManager.getSystem<NavigationSystem>().markDirty();
  auto &editor = m_systemManager.getSystem<EditorUISystem>();
  if (!m_entityManager.isAlive(editor.selectedEntity))
    editor.selectedEntity = -1;
}

void Engine::refreshPinnedMaterials() {
  std::vector<uint32_t> handles;
  auto add = [&](const SceneSnapshot &snapshot) {
    handles.insert(handles.end(), snapshot.materials.begin(), snapshot.materials.end());
  };
  if (m_playSnapshot)
    add(*m_playSnapshot);
  if (m_openCheckpoint)
    add(m_openCheckpoint->snapshot);
  for (const SceneSnapshot &snapshot : m_undoStack)
    add(snapshot);
  for (const SceneSnapshot &snapshot : m_redoStack)
    add(snapshot);
  auto &resourceSystem = m_systemManager.getSystem<ResourceSystem>();
  resourceSystem.setPinnedMaterials(std::move(handles));
  resourceSystem.collectUnusedMaterials();
}

void Engine::applyPlayModeRequest() {
  if (!m_requestedPlayMode)
    return;
  const PlayMode requested = *m_requestedPlayMode;
  m_requestedPlayMode.reset();

  if (requested == PlayMode::Testing && m_playMode != PlayMode::Testing) {
    // Play: everything as it is now, to come back to. An edit still open is not recorded: the history is not used
    // while testing, and Stop brings back the scene as it is now.
    m_openCheckpoint.reset();
    m_historyRequest.reset();
    m_playSnapshot = std::make_unique<SceneSnapshot>(captureScene());
    // Its materials stay alive, and count as used: a game changing one during play changes a copy
    refreshPinnedMaterials();
    m_playMode = PlayMode::Testing;
    LOG_I("[Engine] Play: %zu entities saved", m_entityManager.count());
    return;
  }

  if (m_playMode == PlayMode::Testing && requested != PlayMode::Testing && m_playSnapshot) {
    // Stop: every entity and component back as they were at Play
    SceneSnapshot snapshot = std::move(*m_playSnapshot);
    m_playSnapshot.reset();
    restoreScene(std::move(snapshot));
    m_systemManager.getSystem<ScriptSystem>().reset();
    m_time.clear();
    refreshPinnedMaterials();
    LOG_I("[Engine] Stop: %zu entities restored", m_entityManager.count());
    m_playMode = PlayMode::Editing;
    if (m_app)
      m_app->onPlayStopped(*this);
    return;
  }

  // Freezing and unfreezing without a snapshot (Editing <-> Running)
  if (m_playMode != PlayMode::Testing)
    m_playMode = requested == PlayMode::Testing ? m_playMode : requested;
}

void Engine::beginCheckpoint() {
  if (m_playMode == PlayMode::Testing || m_openCheckpoint)
    return;
  m_openCheckpoint = std::make_unique<Checkpoint>(Checkpoint{captureScene(), getSceneSerializer().fingerprint()});
  refreshPinnedMaterials();
}

void Engine::endCheckpoint() {
  if (!m_openCheckpoint)
    return;
  std::unique_ptr<Checkpoint> checkpoint = std::move(m_openCheckpoint);
  if (checkpoint->fingerprint != getSceneSerializer().fingerprint()) {
    m_redoStack.clear();
    m_undoStack.push_back(std::move(checkpoint->snapshot));
    if (m_undoStack.size() > kMaxUndoSteps)
      m_undoStack.pop_front();
  }
  refreshPinnedMaterials();
}

void Engine::applyHistoryRequest() {
  if (!m_historyRequest)
    return;
  const bool undoing = *m_historyRequest == HistoryRequest::Undo;
  m_historyRequest.reset();
  // Not in the middle of an edit (a drag would lose its footing), and not while testing
  if (m_openCheckpoint || (undoing ? !canUndo() : !canRedo()))
    return;

  SceneSnapshot target;
  if (undoing) {
    target = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    m_redoStack.push_back(captureScene()); // what is about to be replaced is the step the other way
  } else {
    target = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    m_undoStack.push_back(captureScene());
    if (m_undoStack.size() > kMaxUndoSteps)
      m_undoStack.pop_front();
  }

  // The viewport camera stays where it is (flying around is not an edit), unless it is the camera being edited
  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  std::optional<Camera> view;
  if (cameraEntity != -1 && cameraEntity != m_systemManager.getSystem<EditorUISystem>().selectedEntity)
    if (const Camera *camera = m_componentManager.tryGet<Camera>(cameraEntity))
      view = *camera;

  restoreScene(std::move(target));
  // Scripts are left alone: their Lua state stays, and ScriptSystem follows the restored Script components
  if (view && cameraSystem.getActiveCamera() == cameraEntity)
    if (Camera *camera = m_componentManager.tryGet<Camera>(cameraEntity)) {
      camera->position = view->position;
      camera->yaw = view->yaw;
      camera->pitch = view->pitch;
      camera->front = view->front;
      camera->up = view->up;
    }
  refreshPinnedMaterials();
  LOG_I("[Engine] %s: %zu entities", undoing ? "Undo" : "Redo", m_entityManager.count());
}

void Engine::clearHistory() {
  m_openCheckpoint.reset();
  m_historyRequest.reset();
  m_undoStack.clear();
  m_redoStack.clear();
  refreshPinnedMaterials();
}

bool Engine::savePrefab(Entity root, std::string_view path) {
  return getSceneSerializer().savePrefab(root, EngineConfig::resolveAsset(std::string(path)));
}

Entity Engine::instantiate(std::string_view path, glm::vec3 position, glm::vec3 rotation) {
  const std::vector<Entity> created = getSceneSerializer().instantiate(EngineConfig::resolveAsset(std::string(path)));
  // The file's roots (a prefab has one) move to `position`; their children follow below
  Entity first = -1;
  for (Entity entity : created) {
    if (has<Parent>(entity))
      continue;
    if (Transform *transform = tryGet<Transform>(entity)) {
      transform->position += position;
      transform->rotation += rotation;
    }
    if (first == -1)
      first = entity;
  }
  m_systemManager.getSystem<TransformSystem>().updateHierarchy(m_componentManager);
  return first;
}

Entity Engine::findEntity(std::string_view name) {
  Entity found = -1;
  m_componentManager.forEachComponent<Name>([&](Entity entity, Name &entityName) {
    if (found == -1 && entityName.name == name)
      found = entity;
  });
  return found;
}

GameEntity Engine::entity(Entity entity) { return GameEntity(*this, entity); }

GameEntity Engine::find(std::string_view name) {
  const Entity found = findEntity(name);
  return found == -1 ? GameEntity() : GameEntity(*this, found);
}

std::vector<GameEntity> Engine::findAll(std::string_view name) {
  std::vector<Entity> found;
  m_componentManager.forEachComponent<Name>([&](Entity entity, Name &entityName) {
    if (entityName.name == name)
      found.push_back(entity);
  });
  std::sort(found.begin(), found.end());
  std::vector<GameEntity> handles;
  handles.reserve(found.size());
  for (Entity entity : found)
    handles.emplace_back(*this, entity);
  return handles;
}

bool Engine::hasComponent(Entity entity, std::string_view component) {
  if (component == "Model") return has<Model>(entity);
  if (component == "RigidBody") return has<RigidBody>(entity);
  if (component == "CharacterController") return has<CharacterController>(entity);
  if (component == "Light") return has<Light>(entity);
  if (component == "Camera") return has<Camera>(entity);
  if (component == "Animator") return has<Animator>(entity);
  if (component == "Script") return has<Script>(entity);
  if (component == "Transform") return has<Transform>(entity);
  if (component == "AudioSource") return has<AudioSource>(entity);
  if (component == "Skybox") return has<Skybox>(entity);
  if (component == "Collision") return has<Collision>(entity);
  if (component == "ParticleEmitter") return has<ParticleEmitter>(entity);
  if (component == "Hidden") return has<Hidden>(entity);
  if (component == "CameraRig") return has<CameraRig>(entity);
  if (component == "Tags") return has<Tags>(entity);
  for (const ReflectedComponent &reflected : m_reflectedComponents)
    if (reflected.name == component)
      return reflected.get && reflected.get(m_componentManager, entity) != nullptr;
  return false;
}

std::vector<GameEntity> Engine::findTagged(std::string_view tag) {
  std::vector<Entity> found;
  m_componentManager.forEachComponent<Tags>([&](Entity entity, Tags &tags) {
    if (std::find(tags.tags.begin(), tags.tags.end(), tag) != tags.tags.end())
      found.push_back(entity);
  });
  std::sort(found.begin(), found.end());
  std::vector<GameEntity> handles;
  handles.reserve(found.size());
  for (Entity entity : found)
    handles.emplace_back(*this, entity);
  return handles;
}

std::vector<GameEntity> Engine::findWithComponent(std::string_view component) {
  std::vector<Entity> found;
  for (Entity entity : m_entityManager.entities())
    if (hasComponent(entity, component))
      found.push_back(entity);
  std::sort(found.begin(), found.end());
  std::vector<GameEntity> handles;
  handles.reserve(found.size());
  for (Entity entity : found)
    handles.emplace_back(*this, entity);
  return handles;
}

GameEntity Engine::create(std::string_view name, const EntityDesc &desc) {
  auto builder = entities().create(name);
  // "box", "sphere" and "cylinder" are the engine's own models
  if (!desc.model.empty())
    builder.withModel(desc.model == "box"        ? std::string(EngineConfig::MODEL_BOX)
                      : desc.model == "sphere"   ? std::string(EngineConfig::MODEL_SPHERE)
                      : desc.model == "cylinder" ? std::string(EngineConfig::MODEL_CYLINDER)
                                                 : desc.model,
                      desc.position, desc.rotation, desc.scale);
  else
    builder.withTransform(desc.position, desc.rotation, desc.scale);
  if (desc.color)
    builder.withSolidColor(*desc.color);
  if (desc.emission)
    builder.withEmission(*desc.emission, desc.glow);
  if (desc.body)
    builder.withRigidBody(*desc.body, desc.shape, desc.mass);
  if (desc.collision)
    builder.withModelCollision();
  if (!desc.script.empty())
    builder.withScript(desc.script);
  GameEntity created(*this, builder.build());
  for (const std::string &tag : desc.tags)
    created.addTag(tag);
  return created;
}

GameEntity Engine::spawn(std::string_view prefabPath, glm::vec3 position, glm::vec3 rotation) {
  const Entity spawned = instantiate(prefabPath, position, rotation);
  return spawned == -1 ? GameEntity() : GameEntity(*this, spawned);
}

void Engine::setSkyColor(const glm::vec3 &color) {
  Skybox *sky = nullptr;
  m_componentManager.forEachComponent<Skybox>([&](Entity entity, Skybox &skybox) {
    if (!sky && !has<Hidden>(entity))
      sky = &skybox;
  });
  if (sky) {
    sky->color = color;
    return;
  }
  const Entity entity = createEntity();
  addComponent<Name>(entity, "Sky");
  addComponent<Skybox>(entity).color = color;
}

glm::vec3 Engine::getSkyColor() {
  glm::vec3 color(0.0f);
  bool found = false;
  m_componentManager.forEachComponent<Skybox>([&](Entity entity, Skybox &skybox) {
    if (!found && !has<Hidden>(entity)) {
      color = skybox.color;
      found = true;
    }
  });
  return color;
}

void Engine::enablePauseMenu(bool enabled) { m_systemManager.getSystem<PauseMenu>().setEnabled(enabled); }
void Engine::openPauseMenu() { m_systemManager.getSystem<PauseMenu>().open(); }
void Engine::closePauseMenu() { m_systemManager.getSystem<PauseMenu>().close(); }
bool Engine::isPauseMenuOpen() { return m_systemManager.getSystem<PauseMenu>().isOpen(); }

void Engine::setGameName(std::string_view name) { m_systemManager.getSystem<SaveData>().setGameName(name); }
SaveData &Engine::saveData() { return m_systemManager.getSystem<SaveData>(); }

void Engine::setWindowTitle(std::string_view title) {
  SDL_SetWindowTitle(m_systemManager.getSystem<WindowSystem>().getWindow(), std::string(title).c_str());
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
  m_systemManager.insert<SceneSerializer>(m_entityManager, m_componentManager, m_systemManager);
  m_systemManager.insert<ParticleSystem>(m_componentManager);
  m_systemManager.insert<CollisionSystem>();
  m_systemManager.insert<PhysicsSystem>(m_componentManager, m_systemManager.getSystem<ResourceSystem>(),
                                        m_systemManager.getSystem<TransformSystem>());
  m_systemManager.insert<NavigationSystem>(m_componentManager, m_systemManager.getSystem<PhysicsSystem>());
  m_systemManager.insert<AnimationSystem>(m_componentManager, m_systemManager.getSystem<ResourceSystem>());
  m_systemManager.insert<ScriptSystem>(*this);
  m_systemManager.insert<AudioSystem>(m_componentManager);
  m_systemManager.insert<GameUISystem>(m_systemManager.getSystem<WindowSystem>().getWindow());
  m_systemManager.insert<SaveData>();
  m_systemManager.insert<PauseMenu>(*this);
  m_systemManager.insert<EditorUISystem>(m_systemManager.getSystem<WindowSystem>().getWindow(),
                                         m_systemManager.getSystem<WindowSystem>().getContext());
}

bool Engine::loadResources() {
  auto &renderer = m_systemManager.getSystem<RenderSystem>().getRenderer();
  auto &windowSystem = m_systemManager.getSystem<WindowSystem>();
  auto &resourceSystem = m_systemManager.getSystem<ResourceSystem>();
  renderer.init(windowSystem.getWindow());
  TextureProcessing::detectSupport();

  uint32_t baseShader = resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX),
                                                  EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT));
  uint32_t shadowShader = resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX_SHADOW),
                                                    EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT_SHADOW));
  uint32_t particleShader =
      resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX_PARTICLE),
                                EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT_PARTICLE));
  uint32_t maskShader = resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX),
                                                  EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT),
                                                  "#define ALPHA_MASK\n");
  auto fullscreen = [&](const char *fragment) {
    return resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX_FULLSCREEN),
                                     EngineConfig::resolvePath(fragment));
  };
  uint32_t tonemap = fullscreen(EngineConfig::SHADER_FRAGMENT_TONEMAP);
  uint32_t bloomDownsample = fullscreen(EngineConfig::SHADER_FRAGMENT_BLOOM_DOWNSAMPLE);
  uint32_t bloomUpsample = fullscreen(EngineConfig::SHADER_FRAGMENT_BLOOM_UPSAMPLE);
  uint32_t composite = fullscreen(EngineConfig::SHADER_FRAGMENT_COMPOSITE);
  uint32_t skybox = resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX_SKYBOX),
                                              EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT_SKYBOX));
  if (!baseShader || !shadowShader || !particleShader || !maskShader || !tonemap || !bloomDownsample ||
      !bloomUpsample || !composite || !skybox) {
    LOG_E("[Engine] Failed to load shaders.");
    return false;
  }

  m_baseShaderHandle = baseShader;
  resourceSystem.setMaskVariant(baseShader, maskShader);
  resourceSystem.getMaterial(0).setShaderHandle(baseShader);
  m_systemManager.getSystem<RenderSystem>().setShadowShaderHandle(shadowShader);
  m_systemManager.getSystem<RenderSystem>().setPostProcessShaders(tonemap, bloomDownsample, bloomUpsample, composite);
  m_systemManager.getSystem<RenderSystem>().setSkyboxShader(skybox);

  auto &particleSystem = m_systemManager.getSystem<ParticleSystem>();
  particleSystem.setShaderHandle(particleShader);

  return true;
}