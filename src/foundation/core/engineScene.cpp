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

namespace {
const std::string kNoScene;
}

const std::string &Engine::getScenePath() const {
  const EntityManager::Scene *scene = m_entityManager.findScene(m_entityManager.activeScene());
  return scene ? scene->path : kNoScene;
}

std::vector<std::string> Engine::loadedScenes() const {
  std::vector<std::string> paths;
  for (const EntityManager::Scene &scene : m_entityManager.scenes())
    paths.push_back(scene.path);
  return paths;
}

bool Engine::setActiveScene(std::string_view path) {
  const int id = m_entityManager.findSceneByKey(SceneSerializer::sceneKey(std::string(path)));
  if (id == -1)
    return false;
  m_entityManager.setActiveScene(id);
  return true;
}

bool Engine::moveToScene(Entity entity, std::string_view path) {
  const int scene = m_entityManager.findSceneByKey(SceneSerializer::sceneKey(std::string(path)));
  if (scene == -1 || !m_entityManager.isAlive(entity) || m_entityManager.sceneOf(entity) == -1)
    return false;
  if (m_entityManager.sceneOf(entity) == scene)
    return true;
  auto &transformSystem = m_systemManager.getSystem<TransformSystem>();
  if (transformSystem.getParent(m_componentManager, entity) != -1)
    transformSystem.setParent(m_componentManager, entity, -1);
  m_entityManager.setSceneOf(entity, scene);
  for (Entity descendant : transformSystem.getDescendants(m_componentManager, entity))
    if (m_entityManager.sceneOf(descendant) != -1)
      m_entityManager.setSceneOf(descendant, scene);
  return true;
}

bool Engine::isLoadingScene() const {
  return std::any_of(m_pendingScenes.begin(), m_pendingScenes.end(),
                     [](const PendingScene &scene) { return scene.kind != PendingScene::Kind::Unload; });
}

void Engine::markSceneClean(int scene) {
  m_savedHash[scene] = std::hash<std::string>{}(getSceneSerializer().fingerprint(scene));
  m_dirtyValid = false;
}

void Engine::refreshDirty() const {
  if (m_dirtyValid && m_dirtyRevision == m_editRevision)
    return;
  m_dirtyScenes.clear();
  const SceneSerializer &serializer = m_systemManager.getSystem<SceneSerializer>();
  static const size_t kEmpty = std::hash<std::string>{}("[]"); // a scene nothing was made in
  for (const EntityManager::Scene &scene : m_entityManager.scenes()) {
    const auto saved = m_savedHash.find(scene.id);
    if (std::hash<std::string>{}(serializer.fingerprint(scene.id)) != (saved == m_savedHash.end() ? kEmpty : saved->second))
      m_dirtyScenes.insert(scene.id);
  }
  m_dirtyValid = true;
  m_dirtyRevision = m_editRevision;
}

bool Engine::isSceneDirty() const {
  refreshDirty();
  return !m_dirtyScenes.empty();
}

bool Engine::isSceneDirty(std::string_view path) const {
  refreshDirty();
  const int id = m_entityManager.findSceneByKey(SceneSerializer::sceneKey(std::string(path)));
  return id != -1 && m_dirtyScenes.count(id) > 0;
}

std::vector<std::string> Engine::dirtyScenes() const {
  refreshDirty();
  std::vector<std::string> paths;
  for (const EntityManager::Scene &scene : m_entityManager.scenes())
    if (m_dirtyScenes.count(scene.id))
      paths.push_back(scene.path);
  return paths;
}

bool Engine::saveSceneTo(int scene, std::string_view path) {
  const std::string key = SceneSerializer::sceneKey(std::string(path));
  const int other = m_entityManager.findSceneByKey(key);
  if (other != -1 && other != scene) {
    LOG_E("[Engine] Not saving to %s: it is another open scene's file", std::string(path).c_str());
    return false;
  }
  if (!m_systemManager.getSystem<SceneSerializer>().save(std::string(path), false, scene))
    return false;
  m_entityManager.setScenePath(scene, std::string(path), key);
  markSceneClean(scene);
  return true;
}

bool Engine::saveScene(std::string_view path) {
  int scene = m_entityManager.activeScene();
  if (scene == -1)
    scene = m_entityManager.openScene("", "");
  return saveSceneTo(scene, path);
}

bool Engine::saveOpenScene(std::string_view path) {
  const int scene = m_entityManager.findSceneByKey(SceneSerializer::sceneKey(std::string(path)));
  if (scene == -1) {
    LOG_W("[Engine] saveOpenScene: %s is not open", std::string(path).c_str());
    return false;
  }
  return saveSceneTo(scene, m_entityManager.findScene(scene)->path);
}

bool Engine::saveAllScenes() {
  bool ok = true;
  const std::vector<EntityManager::Scene> scenes = m_entityManager.scenes();
  for (const EntityManager::Scene &scene : scenes) {
    if (scene.path.empty()) {
      if (!m_entityManager.entitiesOf(scene.id).empty()) {
        LOG_W("[Engine] saveAllScenes: a scene with no file yet was skipped (saveScene names it)");
        ok = false;
      }
      continue;
    }
    ok = saveSceneTo(scene.id, scene.path) && ok;
  }
  return ok;
}

bool Engine::writeScene(std::string_view path, bool includeRuntime) {
  return m_systemManager.getSystem<SceneSerializer>().save(std::string(path), includeRuntime);
}

SceneSerializer &Engine::getSceneSerializer() { return m_systemManager.getSystem<SceneSerializer>(); }

ReflectedComponent &Engine::addReflectedComponent(ReflectedComponent component) {
  ReflectedComponent &stored = m_reflectedComponents.emplace_back(std::move(component));
  getSceneSerializer().registerReflected(stored);
  return stored;
}

void Engine::queueScene(PendingScene::Kind kind, std::string_view path) {
  PendingScene request;
  request.kind = kind;
  request.path = std::string(path);
  if (kind != PendingScene::Kind::Unload) {
    auto &resources = m_systemManager.getSystem<ResourceSystem>();
    request.models = m_systemManager.getSystem<SceneSerializer>().modelPaths(request.path);
    for (const std::string &model : request.models)
      resources.preloadModel(model); // returns at once for models already loaded or being read
  }
  if (kind == PendingScene::Kind::Replace)
    m_pendingScenes.clear(); // the newer replace wins
  m_pendingScenes.push_back(std::move(request));
}

void Engine::loadScene(std::string_view path) { queueScene(PendingScene::Kind::Replace, path); }

void Engine::loadSceneAdditive(std::string_view path) {
  const std::string key = SceneSerializer::sceneKey(std::string(path));
  // Whether it will be open once what is already asked for is done (a loadScene before this closes the others)
  std::vector<std::string> open;
  for (const EntityManager::Scene &scene : m_entityManager.scenes())
    open.push_back(scene.key);
  for (const PendingScene &pending : m_pendingScenes) {
    const std::string pendingKey = SceneSerializer::sceneKey(pending.path);
    if (pending.kind == PendingScene::Kind::Replace)
      open = {pendingKey};
    else if (pending.kind == PendingScene::Kind::Additive)
      open.push_back(pendingKey);
    else
      open.erase(std::remove(open.begin(), open.end(), pendingKey), open.end());
  }
  if (std::find(open.begin(), open.end(), key) != open.end()) {
    LOG_I("[Engine] loadSceneAdditive: %s is already open", std::string(path).c_str());
    return;
  }
  queueScene(PendingScene::Kind::Additive, path);
}

bool Engine::unloadScene(std::string_view path) {
  const int id = m_entityManager.findSceneByKey(SceneSerializer::sceneKey(std::string(path)));
  if (id == -1 || m_entityManager.scenes().size() <= 1)
    return false;
  queueScene(PendingScene::Kind::Unload, path);
  return true;
}

float Engine::sceneLoadProgress() {
  if (!isLoadingScene())
    return 1.0f;
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  size_t total = 0, ready = 0;
  for (const PendingScene &scene : m_pendingScenes)
    for (const std::string &model : scene.models) {
      ++total;
      if (!resources.isModelPending(model))
        ++ready;
    }
  if (total == 0)
    return 0.0f; // nothing to wait for: the switch is this frame's
  return static_cast<float>(ready) / static_cast<float>(total);
}

void Engine::saveSceneAfter(std::string path, float delaySeconds, bool quitAfter) {
  m_sceneSave = ScreenshotRequest{std::move(path), delaySeconds, quitAfter};
}

void Engine::applyPendingScene() {
  if (m_newScenePending) {
    m_newScenePending = false;
    m_pendingScenes.clear(); // the newer request wins
    m_entityManager.setCreatingRuntime(false); // a scene's own entities, so Save writes them
    m_systemManager.getSystem<SceneSerializer>().clear();
    m_entityManager.closeAllScenes();
    m_systemManager.getSystem<EditorUISystem>().pruneSelection(m_entityManager);
    // Looks down at the origin from a bit above and behind; the light shines from above, a little slanted
    // (the first of them opens the untitled scene)
    createCameraEntity("Camera", glm::vec3(0.0f, 3.0f, 8.0f), 0.0f, -15.0f, 60.0f);
    createLightEntity("Sun", glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(-0.4f, -1.0f, -0.3f), glm::vec3(1.0f),
                      LightType::Directional, 1.0f, 0.0f, 0.0f);
    m_entityManager.setCreatingRuntime(true);
    m_systemManager.getSystem<NavigationSystem>().clear();
    clearHistory();
    m_savedHash.clear();
    markSceneClean(m_entityManager.activeScene());
    return;
  }
  // Waits while the first one's models are read in the background; ResourceSystem::update uploads each as it
  // finishes. Deterministic runs switch on the next frame and wait for the reads there, so the frame does not vary.
  // What a callback asks for in this frame waits for the next one.
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  auto &serializer = m_systemManager.getSystem<SceneSerializer>();
  size_t turns = m_pendingScenes.size();
  while (turns-- > 0 && !m_pendingScenes.empty()) {
    for (const std::string &model : m_pendingScenes.front().models)
      if (!m_deterministic && resources.isModelPending(model))
        return;
    PendingScene request = std::move(m_pendingScenes.front());
    m_pendingScenes.pop_front();
    const std::string key = SceneSerializer::sceneKey(request.path);

    if (request.kind == PendingScene::Kind::Unload) {
      const int id = m_entityManager.findSceneByKey(key);
      if (id == -1 || m_entityManager.scenes().size() <= 1) {
        LOG_W("[Engine] unloadScene: %s is not open, or is the only scene", request.path.c_str());
        continue;
      }
      serializer.unloadScene(id);
      m_savedHash.erase(id);
      m_dirtyValid = false;
      auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
      if (cameraSystem.getActiveCamera() == -1) { // it was in the scene that went: the first camera left takes over
        Entity first = -1;
        m_componentManager.forEachComponent<Camera>([&](Entity entity, Camera &) {
          if (m_entityManager.sceneOf(entity) != -1 && (first == -1 || entity < first))
            first = entity;
        });
        if (first != -1)
          cameraSystem.setActiveCamera(first);
      }
      m_systemManager.getSystem<NavigationSystem>().markDirty();
      m_systemManager.getSystem<EditorUISystem>().pruneSelection(m_entityManager);
      clearHistory();
      continue;
    }

    // What a scene file holds is the scene's, even when the game loads it while it runs
    const bool additive = request.kind == PendingScene::Kind::Additive;
    m_entityManager.setCreatingRuntime(false);
    const bool loaded = additive ? serializer.loadAdditive(request.path) : serializer.load(request.path);
    m_entityManager.setCreatingRuntime(true);
    if (!loaded)
      continue;
    const int id = m_entityManager.findSceneByKey(key);
    if (additive) {
      m_systemManager.getSystem<NavigationSystem>().markDirty();
    } else {
      m_systemManager.getSystem<NavigationSystem>().clear(); // the map was of the old level
      m_camera.applyRememberedFollow();
      m_savedHash.clear();
    }
    clearHistory();
    markSceneClean(id);
    if (m_app)
      m_app->onSceneLoaded(*this, request.path);
  }
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

void Engine::restoreScene(SceneSnapshot snapshot, bool dropRuntime, bool wholeScenes) {
  m_componentManager.restore(std::move(snapshot.components));
  m_entityManager.restore(snapshot.entities, wholeScenes);
  m_dirtyValid = false;
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
    restoreScene(std::move(snapshot), true, true);
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
