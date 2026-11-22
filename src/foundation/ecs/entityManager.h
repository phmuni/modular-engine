#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Entity = int;

class EntityManager {
public:
  // An open scene file. `path` is as it was asked for (empty: no file yet), `key` the same file however it is
  // spelled (resolved, normalized), to tell whether two paths are one file. `id` never repeats.
  struct Scene {
    int id = -1;
    std::string path;
    std::string key;
  };

private:
  Entity m_nextEntity = 0;
  std::unordered_set<Entity> m_activeEntities;
  // Entities the game made while it ran (see Engine::isRuntime); the rest belong to the scene. Copied with the
  // manager, so undo and redo keep who is who.
  std::unordered_set<Entity> m_runtimeEntities;
  bool m_creatingRuntime = true; // not part of what restore() puts back: it says who is creating now

  // Open scenes (additive scenes: several files at once, each entity of the scene in one of them). Copied with the
  // manager, so Play/Stop and undo keep who belongs where. Runtime entities are in none.
  std::vector<Scene> m_scenes; // in the order they were opened
  std::unordered_map<Entity, int> m_sceneOf;
  int m_activeScene = -1;
  int m_nextScene = 0;
  int m_creatingInto = -1; // not restored: the scene the entities being created go to (-1: the active one)

public:
  Entity createEntity();
  void destroyEntity(Entity entity);
  // Puts back the entities `saved` had. Numbers handed out since stay taken, so an entity that existed only in
  // between never shares its number with a later one (per-entity state kept elsewhere, like a script's fields,
  // would mistake the new one for it). Which scene each belongs to comes back too; with `wholeScenes` so does the
  // set of open scenes (their files and which is active), without it (undo and redo: those do not change by
  // editing) only who belongs to which.
  void restore(const EntityManager &saved, bool wholeScenes = true);

  // Whether the entities created from now on are the game's (true at first) or the scene's: the editor and scene
  // loading turn it off around what they create.
  void setCreatingRuntime(bool runtime) { m_creatingRuntime = runtime; }
  bool creatingRuntime() const { return m_creatingRuntime; }
  bool isRuntime(Entity entity) const { return m_runtimeEntities.count(entity) > 0; }
  const std::unordered_set<Entity> &runtimeEntities() const { return m_runtimeEntities; }

  // Scenes. Entities created while creatingRuntime() is off go to the scene being created into (setCreatingInto) or
  // else the active one; with none open an untitled one (no file) is opened for them.
  const std::vector<Scene> &scenes() const { return m_scenes; }
  const Scene *findScene(int id) const;
  int findSceneByKey(const std::string &key) const; // -1 if not open
  int activeScene() const { return m_activeScene; }
  void setActiveScene(int id); // ignored if not open
  int openScene(std::string path, std::string key); // makes it the active one when it is the first
  void setScenePath(int id, std::string path, std::string key);
  // Forgets the scene; its entities are not destroyed (SceneSerializer::unloadScene does), they just belong to none.
  // The active one moves to the first still open.
  void closeScene(int id);
  void closeAllScenes();
  int sceneOf(Entity entity) const;
  void setSceneOf(Entity entity, int id);
  std::vector<Entity> entitiesOf(int id) const; // ascending
  void setCreatingInto(int id) { m_creatingInto = id; }

  bool isAlive(Entity entity) const { return m_activeEntities.count(entity) > 0; }
  std::size_t count() const { return m_activeEntities.size(); }
  const std::unordered_set<Entity> &entities() const { return m_activeEntities; }
};
