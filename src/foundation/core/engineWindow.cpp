// Engine: the window, mouse capture, render scale and graphics settings.

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

void Engine::setMouseCaptured(bool captured) {
  auto &menus = m_systemManager.getSystem<MenuSystem>();
  if (menus.showsCursor())
    menus.setCursorLockOnClose(captured); // a menu has the mouse now; the game gets what it asked for after
  else
    setState(Toggle::CursorLock, captured);
}

bool Engine::isMouseCaptured() { return getState(Toggle::CursorLock); }

void Engine::setWindowTitle(std::string_view title) {
  SDL_SetWindowTitle(m_systemManager.getSystem<WindowSystem>().getWindow(), std::string(title).c_str());
}

namespace {
// Where the window's size and place are kept in the save data
constexpr const char *KEY_WINDOW_X = "engine.window.x";
constexpr const char *KEY_WINDOW_Y = "engine.window.y";
constexpr const char *KEY_WINDOW_WIDTH = "engine.window.width";
constexpr const char *KEY_WINDOW_HEIGHT = "engine.window.height";
} // namespace

void Engine::configureWindow(const WindowConfig &requested) {
  if (m_deterministic || s_deterministicRun)
    return;
  WindowConfig config = requested;
  config.renderScale = std::clamp(config.renderScale, Renderer::kMinRenderScale, Renderer::kMaxRenderScale);
  const bool sized = config.width > 0 && config.height > 0;
  if (m_initialized) {
    if (sized)
      setWindowSize(config.width, config.height);
    setFullscreen(config.fullscreen);
    setRenderScale(config.renderScale);
    return;
  }
  m_windowConfig = config;
  if (sized) {
    m_screenWidth = static_cast<float>(config.width);
    m_screenHeight = static_cast<float>(config.height);
    WindowSystem::setSizeRequested(true);
  }
}

void Engine::setWindowSize(int width, int height) {
  auto &window = m_systemManager.getSystem<WindowSystem>();
  if (!window.setWindowSize(width, height))
    return;
  // The framebuffer follows from the event the system sends, which is read at the next frame: the scene's size is
  // right from here instead
  int pixelWidth = 0, pixelHeight = 0;
  SDL_GetWindowSizeInPixels(window.getWindow(), &pixelWidth, &pixelHeight);
  m_systemManager.getSystem<RenderSystem>().getRenderer().setViewportSize(pixelWidth, pixelHeight);
}

glm::ivec2 Engine::windowSize() { return m_systemManager.getSystem<WindowSystem>().getWindowSize(); }
std::vector<glm::ivec2> Engine::resolutions() { return m_systemManager.getSystem<WindowSystem>().getResolutions(); }
void Engine::setRenderScale(float scale) {
  m_systemManager.getSystem<RenderSystem>().getRenderer().setRenderScale(scale);
}
float Engine::renderScale() { return m_systemManager.getSystem<RenderSystem>().getRenderer().getRenderScale(); }
glm::ivec2 Engine::renderResolution() {
  const Renderer &renderer = m_systemManager.getSystem<RenderSystem>().getRenderer();
  return {renderer.getRenderWidth(), renderer.getRenderHeight()};
}
Renderer &Engine::renderer() { return m_systemManager.getSystem<RenderSystem>().getRenderer(); }
void Engine::setMsaa(int samples) { renderer().setMsaa(samples); }
int Engine::msaa() { return renderer().getMsaa(); }
void Engine::setFxaa(bool enabled) { renderer().setFxaa(enabled); }
bool Engine::fxaa() { return renderer().getFxaa(); }
void Engine::setFsr(bool enabled) { renderer().setFsr(enabled); }
bool Engine::fsr() { return renderer().getFsr(); }
void Engine::setFsrSharpness(float stops) { renderer().setFsrSharpness(stops); }
float Engine::fsrSharpness() { return renderer().getFsrSharpness(); }
void Engine::configureGraphics(const AntialiasingSettings &config) {
  if (m_deterministic || s_deterministicRun)
    return;
  if (m_initialized) {
    setMsaa(config.msaa);
    setFxaa(config.fxaa);
    setFsr(config.fsr);
    setFsrSharpness(config.fsrSharpness);
    return;
  }
  m_graphicsConfig = config;
}
void Engine::setSsao(bool enabled) { m_systemManager.getSystem<RenderSystem>().setSsaoEnabled(enabled); }
bool Engine::ssao() { return m_systemManager.getSystem<RenderSystem>().ssaoEnabled(); }
void Engine::setSsaoSettings(const SsaoSettings &settings) {
  const SsaoSettings clamped = settings.clamped();
  m_systemManager.getSystem<RenderSystem>().setSsaoSettings(clamped);
  m_componentManager.forEachComponent<Skybox>([&](Entity entity, Skybox &skybox) {
    if (has<Hidden>(entity))
      return;
    skybox.ssaoRadius = clamped.radius;
    skybox.ssaoIntensity = clamped.intensity;
  });
}
SsaoSettings Engine::ssaoSettings() {
  SsaoSettings settings = m_systemManager.getSystem<RenderSystem>().ssaoSettings();
  if (const Skybox *sky = findSceneSky()) {
    settings.radius = sky->ssaoRadius;
    settings.intensity = sky->ssaoIntensity;
  }
  return settings.clamped();
}
void Engine::setSsaoDebugView(bool enabled) { m_systemManager.getSystem<RenderSystem>().setSsaoDebugView(enabled); }
bool Engine::ssaoDebugView() { return m_systemManager.getSystem<RenderSystem>().ssaoDebugView(); }
void Engine::setFullscreen(bool enabled) { m_systemManager.getSystem<WindowSystem>().setFullscreen(enabled); }
bool Engine::isFullscreen() { return m_systemManager.getSystem<WindowSystem>().isFullscreen(); }

void Engine::startWindow() {
  auto &window = m_systemManager.getSystem<WindowSystem>();
  // A player's own size and place beat the game's. Not in deterministic runs, whose window is the same every time.
  if (!m_deterministic) {
    SaveData &save = saveData();
    if (save.has(KEY_WINDOW_WIDTH) && save.has(KEY_WINDOW_HEIGHT)) {
      WindowSystem::Placement placement;
      placement.width = static_cast<int>(save.getNumber(KEY_WINDOW_WIDTH));
      placement.height = static_cast<int>(save.getNumber(KEY_WINDOW_HEIGHT));
      placement.x = static_cast<int>(save.getNumber(KEY_WINDOW_X));
      placement.y = static_cast<int>(save.getNumber(KEY_WINDOW_Y));
      window.restorePlacement(placement);
    }
  }
  window.show();
  int pixelWidth = 0, pixelHeight = 0;
  SDL_GetWindowSizeInPixels(window.getWindow(), &pixelWidth, &pixelHeight);
  m_systemManager.getSystem<RenderSystem>().getRenderer().setViewportSize(pixelWidth, pixelHeight);
  m_windowShown = true;
}

void Engine::rememberWindow() {
  if (!m_windowShown || m_deterministic)
    return;
  const auto placement = m_systemManager.getSystem<WindowSystem>().getPlacement();
  if (!placement) // fullscreen: what was kept from the last time in a window stays
    return;
  SaveData &save = saveData();
  save.set(KEY_WINDOW_X, placement->x);
  save.set(KEY_WINDOW_Y, placement->y);
  save.set(KEY_WINDOW_WIDTH, placement->width);
  save.set(KEY_WINDOW_HEIGHT, placement->height);
}

void Engine::captureScreenshot(std::string path, float delaySeconds, bool quitAfter) {
  m_screenshot = ScreenshotRequest{std::move(path), delaySeconds, quitAfter};
}
