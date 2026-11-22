// Engine: scenes, prefabs, the sky, fog and ambient light, play mode, undo and redo.

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

bool Engine::saveScene(std::string_view path) { return writeScene(path, false); }

bool Engine::writeScene(std::string_view path, bool includeRuntime) {
  if (!m_systemManager.getSystem<SceneSerializer>().save(std::string(path), includeRuntime))
    return false;
  m_scenePath = path;
  m_savedRevision = m_editRevision;
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

float Engine::sceneLoadProgress() {
  if (!m_pendingScene)
    return 1.0f;
  if (m_pendingSceneModels.empty())
    return 0.0f; // nothing to wait for: the switch is this frame's
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  size_t ready = 0;
  for (const std::string &model : m_pendingSceneModels)
    if (!resources.isModelPending(model))
      ++ready;
  return static_cast<float>(ready) / static_cast<float>(m_pendingSceneModels.size());
}

void Engine::saveSceneAfter(std::string path, float delaySeconds, bool quitAfter) {
  m_sceneSave = ScreenshotRequest{std::move(path), delaySeconds, quitAfter};
}

void Engine::applyPendingScene() {
  if (m_newScenePending) {
    m_newScenePending = false;
    m_pendingScene.reset(); // the newer request wins
    m_pendingSceneModels.clear();
    m_entityManager.setCreatingRuntime(false); // a scene's own entities, so Save writes them
    m_systemManager.getSystem<SceneSerializer>().clear();
    m_systemManager.getSystem<EditorUISystem>().pruneSelection(m_entityManager);
    // Looks down at the origin from a bit above and behind; the light shines from above, a little slanted
    createCameraEntity("Camera", glm::vec3(0.0f, 3.0f, 8.0f), 0.0f, -15.0f, 60.0f);
    createLightEntity("Sun", glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(-0.4f, -1.0f, -0.3f), glm::vec3(1.0f),
                      LightType::Directional, 1.0f, 0.0f, 0.0f);
    m_entityManager.setCreatingRuntime(true);
    m_scenePath.clear();
    m_systemManager.getSystem<NavigationSystem>().clear();
    clearHistory();
    m_savedRevision = m_editRevision;
    return;
  }
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
  // What a scene file holds is the scene's, even when the game loads it while it runs
  m_entityManager.setCreatingRuntime(false);
  const bool loaded = m_systemManager.getSystem<SceneSerializer>().load(path);
  m_entityManager.setCreatingRuntime(true);
  if (!loaded)
    return;
  m_scenePath = path;
  m_systemManager.getSystem<NavigationSystem>().clear(); // the map was of the old level
  m_camera.applyRememberedFollow();
  clearHistory();
  m_savedRevision = m_editRevision;
  if (m_app)
    m_app->onSceneLoaded(*this, path);
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

void Engine::restoreScene(SceneSnapshot snapshot, bool dropRuntime) {
  m_componentManager.restore(std::move(snapshot.components));
  m_entityManager.restore(snapshot.entities);
  if (dropRuntime) {
    // What the game had made before Play was in the snapshot (the editor was opened over a running game); it goes
    // now, and the game makes it again when it starts over
    const std::vector<Entity> runtime(m_entityManager.runtimeEntities().begin(), m_entityManager.runtimeEntities().end());
    for (Entity entity : runtime) {
      m_componentManager.removeAllComponents(entity);
      m_entityManager.destroyEntity(entity);
    }
  }
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
  editor.pruneSelection(m_entityManager);
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

  if (requested == PlayMode::Testing && !isTesting()) {
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

  if (isTesting() && m_playSnapshot) {
    // Pause and resume keep the snapshot; Paused only freezes the test
    if (requested == PlayMode::Paused) {
      m_playMode = PlayMode::Paused;
      return;
    }
    if (requested == PlayMode::Running || requested == PlayMode::Testing) {
      m_playMode = PlayMode::Testing;
      return;
    }
    // Stop: every entity and component back as they were at Play
    SceneSnapshot snapshot = std::move(*m_playSnapshot);
    m_playSnapshot.reset();
    restoreScene(std::move(snapshot), true);
    m_systemManager.getSystem<ScriptSystem>().reset();
    m_systemManager.getSystem<MenuSystem>().closeAll();
    m_systemManager.getSystem<LoadingScreen>().stop();
    m_time.clear();
    m_tween.clear();
    refreshPinnedMaterials();
    LOG_I("[Engine] Stop: %zu entities restored", m_entityManager.count());
    m_playMode = PlayMode::Editing;
    if (m_app)
      m_app->onPlayStopped(*this);
    return;
  }

  // Freezing and unfreezing without a snapshot (Editing <-> Running): pausing a live game is Editing
  if (!isTesting() && requested != PlayMode::Testing)
    m_playMode = requested == PlayMode::Paused ? PlayMode::Editing : requested;
}

void Engine::beginCheckpoint() {
  if (isTesting() || m_openCheckpoint)
    return;
  m_openCheckpoint = std::make_unique<Checkpoint>(Checkpoint{captureScene(), getSceneSerializer().fingerprint()});
  getSceneSerializer().beginFileJournal(); // prefab files the edit writes belong to the step
  refreshPinnedMaterials();
}

void Engine::endCheckpoint() {
  if (!m_openCheckpoint)
    return;
  std::unique_ptr<Checkpoint> checkpoint = std::move(m_openCheckpoint);
  checkpoint->snapshot.files = getSceneSerializer().endFileJournal();
  if (checkpoint->fingerprint != getSceneSerializer().fingerprint() || !checkpoint->snapshot.files.empty()) {
    m_redoStack.clear();
    m_undoStack.push_back(std::move(checkpoint->snapshot));
    if (m_undoStack.size() > kMaxUndoSteps)
      m_undoStack.pop_front();
    ++m_editRevision;
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
    SceneSnapshot other = captureScene(); // what is about to be replaced is the step the other way
    other.files = target.files;
    m_redoStack.push_back(std::move(other));
  } else {
    target = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    SceneSnapshot other = captureScene();
    other.files = target.files;
    m_undoStack.push_back(std::move(other));
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

  // The prefab files go back first, so the cache is right when the restored instances are next compared with it
  for (const PrefabFileEdit &edit : target.files)
    getSceneSerializer().restorePrefabFile(edit.path, undoing ? edit.before : edit.after);
  restoreScene(std::move(target));
  ++m_editRevision;
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
  getSceneSerializer().endFileJournal();
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

Skybox &Engine::sceneSky() {
  Skybox *sky = nullptr;
  m_componentManager.forEachComponent<Skybox>([&](Entity entity, Skybox &skybox) {
    if (!sky && !has<Hidden>(entity))
      sky = &skybox;
  });
  if (sky)
    return *sky;
  const Entity entity = createEntity();
  addComponent<Name>(entity, "Sky");
  return addComponent<Skybox>(entity);
}

const Skybox *Engine::findSceneSky() {
  const Skybox *sky = nullptr;
  m_componentManager.forEachComponent<Skybox>([&](Entity entity, Skybox &skybox) {
    if (!sky && !has<Hidden>(entity))
      sky = &skybox;
  });
  return sky;
}

void Engine::setSkyColor(const glm::vec3 &color) { sceneSky().color = color; }

glm::vec3 Engine::skyColor() {
  const Skybox *sky = findSceneSky();
  return sky ? sky->color : glm::vec3(0.0f);
}

void Engine::setFog(const Fog &fog) { sceneSky().fog = fog; }

Fog Engine::fog() {
  const Skybox *sky = findSceneSky();
  return sky ? sky->fog : Fog();
}

void Engine::setAmbient(const glm::vec3 &color, float intensity) {
  Skybox &sky = sceneSky();
  sky.ambientColor = color;
  sky.ambientIntensity = intensity;
}

glm::vec3 Engine::ambientColor() {
  const Skybox *sky = findSceneSky();
  return sky ? sky->ambientColor : glm::vec3(1.0f);
}

float Engine::ambientIntensity() {
  const Skybox *sky = findSceneSky();
  return sky ? sky->ambientIntensity : 0.0f;
}
