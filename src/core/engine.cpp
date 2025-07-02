#include "core/engine.h"
#include "component/nameComponent.h"
#include "system/cameraSystem.h"
#include "system/inputSystem.h"
#include "system/uiSystem.h"

// ================================
//  Destructor and Initialization
// ================================
Engine::~Engine() { SDL_Quit(); }

bool Engine::initialize() {
  if (!windowManager.initialize(m_screenWidth, m_screenHeight)) {
    SDL_Log("Failed to initialize window and context.");
    return false;
  }

  registerSystems();

  if (!loadResources()) {
    SDL_Log("Failed to load resources.");
    return false;
  }

  return true;
}

void Engine::registerSystems() {
  systemManager.registerSystem<RenderSystem>();
  systemManager.registerSystem<TransformSystem>();
  systemManager.registerSystem<InputSystem>();
  systemManager.registerSystem<LightSystem>();
  systemManager.registerSystem<TimeSystem>();
  systemManager.registerSystem<CameraSystem>(componentManager, systemManager.getSystem<InputSystem>());
  systemManager.registerSystem<UISystem>(windowManager.getWindow(), windowManager.getContext());
}

bool Engine::loadShaderOrLog(const std::string &name, const std::string &vertexPath, const std::string &fragmentPath) {
  if (!shaderManager.loadShader(name.c_str(), vertexPath.c_str(), fragmentPath.c_str())) {
    SDL_Log("Failed to load shader '%s'!", name.c_str());
    return false;
  }
  return true;
}

bool Engine::loadResources() {
  auto &renderer = systemManager.getSystem<RenderSystem>().getRenderer();
  renderer.initialize(windowManager.getWindow());

  return loadShaderOrLog("base", "../assets/shaders/vertexShader.glsl", "../assets/shaders/fragmentShader.glsl") &&
         loadShaderOrLog("shadow", "../assets/shaders/vertexShadowShader.glsl",
                         "../assets/shaders/fragmentShadowShader.glsl");
}

// ================================
//  Main Loop
// ================================
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
  auto &input = systemManager.getSystem<InputSystem>();
  auto &time = systemManager.getSystem<TimeSystem>();
  auto &camera = systemManager.getSystem<CameraSystem>();

  input.update(&running);
  time.update();
  camera.update(cameraManager.getActiveCamera(), time.getDeltaTime(), systemManager, windowManager);
}

void Engine::render() {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &uiSystem = systemManager.getSystem<UISystem>();

  renderSystem.getRenderer().beginFrame();

  uiSystem.beginFrame();

  renderSystem.renderCall(shaderManager, systemManager, entityManager, componentManager, cameraManager);

  uiSystem.render(systemManager, componentManager);
  uiSystem.endFrame();

  renderSystem.getRenderer().endFrame();
}

// ================================
//  Entity Creation
// ================================

void Engine::createCamera(glm::vec3 position, float yaw, float pitch, float fov) {
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  Entity cameraEntity = entityManager.createEntity();

  auto cameraComponent = std::make_shared<CameraComponent>();
  cameraComponent->position = position;
  cameraComponent->yaw = yaw;
  cameraComponent->pitch = pitch;
  cameraComponent->fov = fov;

  cameraSystem.updateFront(*cameraComponent);

  componentManager.add<CameraComponent>(cameraEntity, cameraComponent);
  cameraManager.setActiveCamera(cameraEntity);
}

void Engine::createEntityWithModel(const std::string name, const std::string &modelPath, const std::string &texturePath,
                                   glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) {
  Entity entity = entityManager.createEntity();

  auto material = MaterialLoader::loadMaterial(texturePath, 32.0f);
  if (!material) {
    SDL_Log("Failed to load material from: %s", texturePath.c_str());
    return;
  }

  auto mesh = MeshLoader::loadFromOBJ(modelPath);
  if (!mesh) {
    SDL_Log("Failed to load mesh from: %s", modelPath.c_str());
    return;
  }

  componentManager.add<NameComponent>(entity, std::make_shared<NameComponent>(name));
  componentManager.add<TransformComponent>(entity, std::make_shared<TransformComponent>(position, rotation, scale));
  componentManager.add<ModelComponent>(entity, std::make_shared<ModelComponent>(mesh, material));

  systemManager.getSystem<RenderSystem>().addRenderable(entity);
}

void Engine::createEntityWithLight(glm::vec3 position, glm::vec3 direction, glm::vec3 color, LightType type,
                                   float intensity, float cutOff, float outerCutOff) {
  Entity entity = entityManager.createEntity();

  auto light = std::make_shared<LightComponent>();
  *light = {type, position, direction, color, intensity, 0.2f, 1.0f, 0.09f, 0.032f, cutOff, outerCutOff};

  componentManager.add<LightComponent>(entity, light);
  systemManager.getSystem<LightSystem>().addLight(entity);
}
