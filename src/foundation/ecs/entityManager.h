#pragma once

#include <cstddef>
#include <unordered_set>

using Entity = int;

class EntityManager {
private:
  Entity m_nextEntity = 0;
  std::unordered_set<Entity> m_activeEntities;
  // Entities the game made while it ran (see Engine::isRuntime); the rest belong to the scene. Copied with the
  // manager, so undo and redo keep who is who.
  std::unordered_set<Entity> m_runtimeEntities;
  bool m_creatingRuntime = true; // not part of what restore() puts back: it says who is creating now

public:
  Entity createEntity();
  void destroyEntity(Entity entity);
  // Puts back the entities `saved` had. Numbers handed out since stay taken, so an entity that existed only in
  // between never shares its number with a later one (per-entity state kept elsewhere, like a script's fields,
  // would mistake the new one for it).
  void restore(const EntityManager &saved);

  // Whether the entities created from now on are the game's (true at first) or the scene's: the editor and scene
  // loading turn it off around what they create.
  void setCreatingRuntime(bool runtime) { m_creatingRuntime = runtime; }
  bool creatingRuntime() const { return m_creatingRuntime; }
  bool isRuntime(Entity entity) const { return m_runtimeEntities.count(entity) > 0; }
  const std::unordered_set<Entity> &runtimeEntities() const { return m_runtimeEntities; }

  bool isAlive(Entity entity) const { return m_activeEntities.count(entity) > 0; }
  std::size_t count() const { return m_activeEntities.size(); }
  const std::unordered_set<Entity> &entities() const { return m_activeEntities; }
};
