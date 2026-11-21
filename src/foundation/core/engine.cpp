// Core engine class managing the main loop, system registration, and resource loading.

#include "foundation/core/engine.h"

#include "components/animationStateMachine.h"
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
#include "systems/animationGraph.h"
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
#include "systems/loadingScreen.h"
#include "systems/menu.h"
#include "systems/pauseMenu.h"
#include "systems/playerSettings.h"
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
#include <exception>
#include <utility>
#include <filesystem>
#include <limits>

Engine::Engine()
    : m_screenWidth(EngineConfig::DEFAULT_SCREEN_WIDTH), m_screenHeight(EngineConfig::DEFAULT_SCREEN_HEIGHT) {}

Engine::~Engine() { shutdown(); }

bool Engine::initialize() {
  Logger::setLevel(Logger::Level::Info);

  registerSystems();

  m_initialized = loadResources();
  return m_initialized;
}

void Engine::run(App &app) {
  m_app = &app;
  try {
    // The game's defaults first (project.json's, configured before initialize), then what the player saved over them,
    // then the game's setup. The window's saved size and place come with startWindow, once setup is done.
    if (m_windowConfig.fullscreen)
      m_systemManager.getSystem<WindowSystem>().setFullscreen(true);
    m_systemManager.getSystem<PlayerSettings>().captureDefaults();
    m_systemManager.getSystem<PlayerSettings>().applySaved();
    m_app->setup(*this);
    startWindow();
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

  rememberWindow(); // before the save data is written
  m_systemManager.getSystem<SaveData>().writeIfChanged();
  // Attempt to clean up renderer and SDL; the game UI frees its GPU resources first, while the context is alive
  m_systemManager.getSystem<GameUISystem>().shutdown();
  auto &renderSystem = m_systemManager.getSystem<RenderSystem>();
  renderSystem.shutdownSsao();
  renderSystem.getRenderer().shutdown();
  SDL_Quit();
}

void Engine::loop(bool &isRunning) {
  // The first frame measures from here, not from engine start-up (which includes the app's setup)
  m_systemManager.getSystem<TimeSystem>().begin();
  SDL_AddEventWatch(&Engine::onSdlEvent, this);
  m_loopRunning = true;
  while (isRunning) {
    update(isRunning);
    if (m_liveFrameError)
      break;
    render();
    if (m_benchmark)
      recordBenchmarkFrame();
  }
  m_loopRunning = false;
  SDL_RemoveEventWatch(&Engine::onSdlEvent, this);
  if (m_liveFrameError)
    std::rethrow_exception(std::exchange(m_liveFrameError, nullptr));
}

bool SDLCALL Engine::onSdlEvent(void *userdata, SDL_Event *event) {
  Engine &engine = *static_cast<Engine *>(userdata);
  // Only from the event pump of the main loop (not, say, a fullscreen switch in a handler), and never in a
  // deterministic run, whose frame count is its output
  if (!engine.m_loopRunning || engine.m_inLiveFrame || engine.m_deterministic || !SDL_IsMainThread() ||
      !engine.m_systemManager.getSystem<InputSystem>().isPolling())
    return true;
  if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
    // The next frame draws at the new size before the queued event is read
    auto &renderer = engine.m_systemManager.getSystem<RenderSystem>().getRenderer();
    engine.m_systemManager.getSystem<WindowSystem>().onResize(event->window.data1, event->window.data2, renderer);
  } else if (event->type == SDL_EVENT_WINDOW_EXPOSED) {
    engine.runFrameInEventPump();
  }
  return true;
}

void Engine::runFrameInEventPump() {
  m_inLiveFrame = true;
  try {
    bool running = true;
    update(running, false);
    render();
  } catch (...) {
    m_liveFrameError = std::current_exception();
    m_quitRequested = true;
  }
  m_inLiveFrame = false;
}

void Engine::setDeterministic() {
  m_deterministic = true;
  s_deterministicRun = true;
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


void Engine::update(bool &isRunning, bool pollInput) {
  m_frameStartNs = SDL_GetTicksNS();

  auto &inputSystem = m_systemManager.getSystem<InputSystem>();
  // The editor has the camera while the game is frozen for editing: the right button flies it (or, with the free
  // camera on in Settings, in any mode)
  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  {
    const auto &toggles = m_systemManager.getSystem<StateSystem>();
    cameraSystem.setEditorNavigation(m_playMode == PlayMode::Editing, toggles.isToggled(Toggle::ShowUI));
    inputSystem.setEditorFlyAllowed(cameraSystem.canNavigate(toggles));
  }
  if (pollInput)
    inputSystem.update(&isRunning, m_systemManager);
  else
    inputSystem.clearFrameState();

  // Play, Stop and freezing, from the editor's last frame
  applyPlayModeRequest();
  applyHistoryRequest();

  // Before the pause state is read below; while it is open the game sees no input
  auto &menus = m_systemManager.getSystem<MenuSystem>();
  menus.update(inputSystem.wasEscapePressed());
  inputSystem.setGameInputBlocked(menus.blocksInput());

  auto &state = m_systemManager.getSystem<StateSystem>();
  // A cursor the game hid comes back where it has to be clicked
  m_systemManager.getSystem<WindowSystem>().setCursorVisible(m_cursorVisible || menus.showsCursor() ||
                                                              state.isToggled(Toggle::ShowUI));
  // Upload models whose background reads finished
  m_systemManager.getSystem<ResourceSystem>().update();

  auto &timeSystem = m_systemManager.getSystem<TimeSystem>();
  timeSystem.setPaused(state.isToggled(Toggle::Paused) || m_playMode == PlayMode::Editing);
  timeSystem.update();

  // The camera runs on real time, so it can still fly around a paused scene
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

  // Prefab files edited while the editor is open: their linked instances follow. A stat per prefab in use, a few
  // times a second, and never in a game without the editor up.
  if (editorUI.m_renderUI && m_playMode != PlayMode::Testing) {
    m_prefabPollTime += timeSystem.getUnscaledDeltaTime();
    if (m_prefabPollTime >= kPrefabPollSeconds) {
      m_prefabPollTime = 0.0f;
      getSceneSerializer().refreshPrefabs();
      // Custom shader files edited while the editor is open: they compile again and the parts using them follow
      if (m_systemManager.getSystem<ResourceSystem>().reloadChangedShaders())
        m_systemManager.getSystem<RenderSystem>().markBatchesDirty();
    }
  }
  // Custom shaders' `time` while the game is frozen in the editor, so a water surface still moves there; a running
  // game uses its own clock (and so is repeatable under --deterministic)
  if (m_playMode == PlayMode::Editing)
    m_previewTime += timeSystem.getUnscaledDeltaTime();

  if (m_playMode != PlayMode::Editing) {
    m_time.tick(timeSystem.getDeltaTime()); // the game clock and timers, before the app
    m_tween.tick(timeSystem.getDeltaTime());
  }
  if (m_app && m_playMode != PlayMode::Editing) {
    m_app->update(*this, timeSystem.getDeltaTime());
  }
  // Lua scripts, after the app (frozen while editing, like it)
  if (m_playMode != PlayMode::Editing)
    m_systemManager.getSystem<ScriptSystem>().update(timeSystem.getDeltaTime());

  // Scene switches wait until the app is done with this frame's entities
  applyPendingScene();
  // The loading screen follows what that left pending
  m_systemManager.getSystem<LoadingScreen>().update(timeSystem.getUnscaledDeltaTime(), isLoadingScene(),
                                                     sceneLoadProgress());

  // Bodies fall, collide and push; then children follow where their parents went this frame
  m_systemManager.getSystem<PhysicsSystem>().update(timeSystem.getDeltaTime());
  m_systemManager.getSystem<NavigationSystem>().update();
  m_systemManager.getSystem<TransformSystem>().updateHierarchy(m_componentManager);
  // Skeletons posed from their clips (time stands still while paused or editing)
  auto &animationSystem = m_systemManager.getSystem<AnimationSystem>();
  animationSystem.update(timeSystem.getDeltaTime());
  // State machines that changed state: C++ listeners, then the entity's script
  for (const AnimationSystem::StateChange &change : animationSystem.takeStateChanges()) {
    if (!isAlive(change.entity))
      continue;
    for (const auto &listener : m_animationListeners)
      listener(entity(change.entity), change.from, change.to);
    m_systemManager.getSystem<ScriptSystem>().animationStateChanged(change.entity, change.from, change.to);
  }
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

  renderSystem.setShaderTime(m_playMode == PlayMode::Editing ? m_previewTime : m_time.elapsed());

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

Camera *Engine::getActiveCamera() {
  Entity camera = m_systemManager.getSystem<CameraSystem>().getActiveCamera();
  return camera == -1 ? nullptr : m_componentManager.tryGet<Camera>(camera);
}

InputSystem &Engine::input() { return m_systemManager.getSystem<InputSystem>(); }

PhysicsSystem &Engine::physics() { return m_systemManager.getSystem<PhysicsSystem>(); }

NavigationSystem &Engine::navigation() { return m_systemManager.getSystem<NavigationSystem>(); }

ScriptSystem &Engine::scripts() { return m_systemManager.getSystem<ScriptSystem>(); }

bool Engine::getState(Toggle toggle) { return m_systemManager.getSystem<StateSystem>().isToggled(toggle); }

void Engine::setState(Toggle toggle, bool value) {
  auto &stateSystem = m_systemManager.getSystem<StateSystem>();
  stateSystem.setToggle(toggle, value);
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

void Engine::setMusicVolume(float volume, uint32_t music) {
  m_systemManager.getSystem<AudioSystem>().setMusicVolume(volume, music);
}

uint32_t Engine::playMusic(std::string_view path, float volume, bool loop) {
  return m_systemManager.getSystem<AudioSystem>().playMusic(std::string(path), volume, loop);
}

bool Engine::isMusicPlaying(uint32_t music) { return m_systemManager.getSystem<AudioSystem>().isMusicPlaying(music); }

Rml::Context *Engine::getUIContext() { return m_systemManager.getSystem<GameUISystem>().getContext(); }

Rml::ElementDocument *Engine::loadUIDocument(std::string_view path, bool show) {
  return m_systemManager.getSystem<GameUISystem>().loadDocument(std::string(path), show);
}

bool Engine::loadUIFont(std::string_view path, bool fallback) {
  return m_systemManager.getSystem<GameUISystem>().loadFont(std::string(path), fallback);
}

void Engine::stopMusic(uint32_t music) { m_systemManager.getSystem<AudioSystem>().stopMusic(music); }

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
    } else if (arg == "--script" && i + 1 < argc) {
      result.scriptPath = std::filesystem::absolute(argv[++i]).string();
    } else if (arg == "--deterministic") {
      result.deterministic = true;
      s_deterministicRun = true;
      WindowSystem::setHighDensity(false); // the same pixels on any screen: the window is created after this
      WindowSystem::setUnattended(true);   // nobody to bring the window to the front
      AudioSystem::setNullBackend(true);   // nobody to listen: skip the sound card
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
         "  --deterministic                     Fixed 1/60 s frames and seeded randomness, for repeatable runs\n"
         "  --script <file.lua>                 Run a Lua script beside the game (onStart, onUpdate, engine.run)\n";
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
  if (!commandLine.scriptPath.empty())
    scripts().runSideScript(commandLine.scriptPath);
}

void Engine::enablePauseMenu(bool enabled) { pauseMenu().setEnabled(enabled); }
PauseMenu &Engine::pauseMenu() { return m_systemManager.getSystem<MenuSystem>().pauseMenu(); }
MenuSystem &Engine::menus() { return m_systemManager.getSystem<MenuSystem>(); }
LoadingScreen &Engine::loadingScreen() { return m_systemManager.getSystem<LoadingScreen>(); }
PlayerSettings &Engine::settings() { return m_systemManager.getSystem<PlayerSettings>(); }

void Engine::setGameName(std::string_view name) {
  m_systemManager.getSystem<SaveData>().setGameName(name);
  // A game that names itself in setup has the player's choices from its own save applied now (the engine applied those
  // of the default name before setup); before run() they are applied there
  if (m_app)
    m_systemManager.getSystem<PlayerSettings>().applySaved();
}
SaveData &Engine::saveData() { return m_systemManager.getSystem<SaveData>(); }

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
  m_systemManager.getSystem<SceneSerializer>().setCollisionLayers(m_layers);
  m_systemManager.insert<ParticleSystem>(m_componentManager);
  m_systemManager.insert<CollisionSystem>(m_layers);
  m_systemManager.insert<PhysicsSystem>(m_componentManager, m_systemManager.getSystem<ResourceSystem>(),
                                        m_systemManager.getSystem<TransformSystem>(), m_layers);
  m_systemManager.insert<NavigationSystem>(m_componentManager, m_systemManager.getSystem<PhysicsSystem>());
  m_systemManager.insert<AnimationSystem>(m_componentManager, m_systemManager.getSystem<ResourceSystem>());
  m_systemManager.insert<ScriptSystem>(*this);
  m_systemManager.insert<AudioSystem>(m_componentManager);
  m_systemManager.insert<GameUISystem>(m_systemManager.getSystem<WindowSystem>().getWindow());
  m_systemManager.insert<SaveData>();
  m_systemManager.insert<PlayerSettings>(*this);
  m_systemManager.insert<MenuSystem>(*this);
  m_systemManager.insert<LoadingScreen>(*this);
  m_systemManager.insert<EditorUISystem>(m_systemManager.getSystem<WindowSystem>().getWindow(),
                                         m_systemManager.getSystem<WindowSystem>().getContext());
}

bool Engine::loadResources() {
  auto &renderer = m_systemManager.getSystem<RenderSystem>().getRenderer();
  auto &windowSystem = m_systemManager.getSystem<WindowSystem>();
  auto &resourceSystem = m_systemManager.getSystem<ResourceSystem>();
  renderer.setRenderScale(m_windowConfig.renderScale); // before init: it makes the targets at that size
  renderer.setMsaa(m_graphicsConfig.msaa);              // ... and with that many samples
  renderer.setFxaa(m_graphicsConfig.fxaa);
  renderer.setFsr(m_graphicsConfig.fsr);
  renderer.setFsrSharpness(m_graphicsConfig.fsrSharpness);
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
  uint32_t fxaa = fullscreen(EngineConfig::SHADER_FRAGMENT_FXAA);
  uint32_t fsrEasu = fullscreen(EngineConfig::SHADER_FRAGMENT_FSR_EASU);
  uint32_t fsrRcas = fullscreen(EngineConfig::SHADER_FRAGMENT_FSR_RCAS);
  uint32_t skybox = resourceSystem.loadShader(EngineConfig::resolvePath(EngineConfig::SHADER_VERTEX_SKYBOX),
                                              EngineConfig::resolvePath(EngineConfig::SHADER_FRAGMENT_SKYBOX));
  if (!baseShader || !shadowShader || !particleShader || !maskShader || !tonemap || !bloomDownsample ||
      !bloomUpsample || !composite || !fxaa || !fsrEasu || !fsrRcas || !skybox) {
    LOG_E("[Engine] Failed to load shaders.");
    return false;
  }

  m_baseShaderHandle = baseShader;
  resourceSystem.setMaskVariant(baseShader, maskShader);
  resourceSystem.getMaterial(0).setShaderHandle(baseShader);
  m_systemManager.getSystem<RenderSystem>().setShadowShaderHandle(shadowShader);
  m_systemManager.getSystem<RenderSystem>().setPostProcessShaders(tonemap, bloomDownsample, bloomUpsample, composite);
  m_systemManager.getSystem<RenderSystem>().setAntialiasingShaders(fxaa, fsrEasu, fsrRcas);
  m_systemManager.getSystem<RenderSystem>().setSkyboxShader(skybox);

  auto &particleSystem = m_systemManager.getSystem<ParticleSystem>();
  particleSystem.setShaderHandle(particleShader);

  return true;
}
