
// Entity creation and destruction.

#include "foundation/ecs/entityManager.h"
#include <algorithm>
#include <iterator>

Entity EntityManager::createEntity() {
  Entity entity = m_nextEntity++;
  m_activeEntities.insert(entity);
  if (m_creatingRuntime) {
    m_runtimeEntities.insert(entity);
  } else {
    int scene = m_creatingInto != -1 && findScene(m_creatingInto) ? m_creatingInto : m_activeScene;
    if (scene == -1)
      scene = openScene("", "");
    m_sceneOf[entity] = scene;
  }
  return entity;
}

void EntityManager::destroyEntity(Entity entity) {
  m_activeEntities.erase(entity);
  m_runtimeEntities.erase(entity);
  m_sceneOf.erase(entity);
}

void EntityManager::restore(const EntityManager &saved, bool wholeScenes) {
  const Entity next = std::max(m_nextEntity, saved.m_nextEntity);
  const int nextScene = std::max(m_nextScene, saved.m_nextScene);
  const bool creatingRuntime = m_creatingRuntime;
  const int creatingInto = m_creatingInto;
  std::vector<Scene> scenes = std::move(m_scenes);
  const int active = m_activeScene;
  *this = saved;
  m_nextEntity = next;
  m_nextScene = nextScene;
  m_creatingRuntime = creatingRuntime;
  m_creatingInto = creatingInto;
  if (!wholeScenes) {
    m_scenes = std::move(scenes);
    m_activeScene = active;
    for (auto it = m_sceneOf.begin(); it != m_sceneOf.end();) // a scene closed since: its entities belong to none
      it = findScene(it->second) ? std::next(it) : m_sceneOf.erase(it);
  }
}

const EntityManager::Scene *EntityManager::findScene(int id) const {
  for (const Scene &scene : m_scenes)
    if (scene.id == id)
      return &scene;
  return nullptr;
}

int EntityManager::findSceneByKey(const std::string &key) const {
  for (const Scene &scene : m_scenes)
    if (scene.key == key)
      return scene.id;
  return -1;
}

void EntityManager::setActiveScene(int id) {
  if (findScene(id))
    m_activeScene = id;
}

int EntityManager::openScene(std::string path, std::string key) {
  const int id = m_nextScene++;
  m_scenes.push_back(Scene{id, std::move(path), std::move(key)});
  if (m_activeScene == -1)
    m_activeScene = id;
  return id;
}

void EntityManager::setScenePath(int id, std::string path, std::string key) {
  for (Scene &scene : m_scenes)
    if (scene.id == id) {
      scene.path = std::move(path);
      scene.key = std::move(key);
    }
}

void EntityManager::closeScene(int id) {
  m_scenes.erase(std::remove_if(m_scenes.begin(), m_scenes.end(), [&](const Scene &scene) { return scene.id == id; }),
                 m_scenes.end());
  for (auto it = m_sceneOf.begin(); it != m_sceneOf.end();)
    it = it->second == id ? m_sceneOf.erase(it) : std::next(it);
  if (m_activeScene == id)
    m_activeScene = m_scenes.empty() ? -1 : m_scenes.front().id;
}

void EntityManager::closeAllScenes() {
  m_scenes.clear();
  m_sceneOf.clear();
  m_activeScene = -1;
}

int EntityManager::sceneOf(Entity entity) const {
  auto it = m_sceneOf.find(entity);
  return it == m_sceneOf.end() ? -1 : it->second;
}

void EntityManager::setSceneOf(Entity entity, int id) {
  if (m_activeEntities.count(entity) && !m_runtimeEntities.count(entity) && findScene(id))
    m_sceneOf[entity] = id;
}

std::vector<Entity> EntityManager::entitiesOf(int id) const {
  std::vector<Entity> entities;
  for (const auto &[entity, scene] : m_sceneOf)
    if (scene == id)
      entities.push_back(entity);
  std::sort(entities.begin(), entities.end());
  return entities;
}
