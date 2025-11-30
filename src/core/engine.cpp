#include "core/engine.h"
#include "core/config.h"

#include "system/cameraSystem.h"
#include "system/inputSystem.h"
#include "system/lightSystem.h"
#include "system/renderSystem.h"
#include "system/sceneSystem.h"
#include "system/shaderSystem.h"
#include "system/timeSystem.h"
#include "system/transformSystem.h"
#include "system/uiSystem.h"
#include "system/windowSystem.h"

Engine::~Engine() { SDL_Quit(); }

bool Engine::initialize() {

  registerSystems();

  if (!loadResources()) {
    SDL_Log("Failed to load resources.");
    return false;
  }

  return true;
}

void Engine::registerSystems() {
  systemManager.registerSystem<WindowSystem>(m_defaultScreenWidth, m_defaultScreenHeight);
  systemManager.registerSystem<RenderSystem>();
  systemManager.registerSystem<TransformSystem>();
  systemManager.registerSystem<InputSystem>();
  systemManager.registerSystem<LightSystem>();
  systemManager.registerSystem<TimeSystem>();
  systemManager.registerSystem<ShaderSystem>();
  systemManager.registerSystem<SceneSystem>(entityManager, componentManager, systemManager);
  systemManager.registerSystem<CameraSystem>(componentManager, systemManager.getSystem<InputSystem>());
  systemManager.registerSystem<UISystem>(systemManager.getSystem<WindowSystem>().getWindow(),
                                         systemManager.getSystem<WindowSystem>().getContext());
}

bool Engine::loadShaderOrLog(const std::string &name, const std::string &vertexPath, const std::string &fragmentPath) {
  auto &shaderSystem = systemManager.getSystem<ShaderSystem>();
  if (!shaderSystem.loadShader(name.c_str(), vertexPath.c_str(), fragmentPath.c_str())) {
    SDL_Log("Failed to load shader '%s'!", name.c_str());
    return false;
  }
  return true;
}

bool Engine::loadResources() {
  auto &renderer = systemManager.getSystem<RenderSystem>().getRenderer();
  auto &windowSystem = systemManager.getSystem<WindowSystem>();
  renderer.initialize(windowSystem.getWindow());

  return loadShaderOrLog("base", EngineConfig::SHADER_VERTEX, EngineConfig::SHADER_FRAGMENT) &&
         loadShaderOrLog("shadow", EngineConfig::SHADER_VERTEX_SHADOW, EngineConfig::SHADER_FRAGMENT_SHADOW);
}

void Engine::run() {
  bool running = true;
  mainLoop(running);
}

void Engine::mainLoop(bool &running) {
  while (running) {
    update(running);
    render();
  }
}

void Engine::update(bool &running) {
  auto &inputSystem = systemManager.getSystem<InputSystem>();
  auto &timeSystem = systemManager.getSystem<TimeSystem>();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  auto &windowSystem = systemManager.getSystem<WindowSystem>();

  inputSystem.update(&running, systemManager);
  timeSystem.update();
  cameraSystem.update(timeSystem.getDeltaTime(), systemManager);
}

void Engine::render() {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &renderer = renderSystem.getRenderer();
  auto &uiSystem = systemManager.getSystem<UISystem>();

  renderer.beginFrame();

  uiSystem.beginFrame();

  renderSystem.renderCall(systemManager, entityManager, componentManager);

  uiSystem.render(entityManager, systemManager, componentManager);
  uiSystem.endFrame();

  renderer.endFrame();
}

void Engine::createEntityCamera(glm::vec3 position, float yaw, float pitch, float fov) {
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  sceneSystem.createEntityCamera(position, yaw, pitch, fov);
}

void Engine::createEntityModel(const std::string &name, const std::string &modelPath, const std::string &texturePath,
                               glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) {
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  sceneSystem.createEntityModel(name, modelPath, texturePath, position, rotation, scale);
}

void Engine::createEntityLight(const std::string &name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                               LightType type, float intensity, float cutOff, float outerCutOff) {
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  sceneSystem.createEntityLight(name, position, direction, color, type, intensity, cutOff, outerCutOff);
}
