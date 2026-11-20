#pragma once

#include <cstddef>
#include <unordered_set>

using Entity = int;

class EntityManager {
private:
  Entity m_nextEntity = 0;
  std::unordered_set<Entity> m_activeEntities;

public:
  Entity createEntity();
  void destroyEntity(Entity entity);
  // Puts back the entities `saved` had. Numbers handed out since stay taken, so an entity that existed only in
  // between never shares its number with a later one (per-entity state kept elsewhere, like a script's fields,
  // would mistake the new one for it).
  void restore(const EntityManager &saved);

  bool isAlive(Entity entity) const { return m_activeEntities.count(entity) > 0; }
  std::size_t count() const { return m_activeEntities.size(); }
  const std::unordered_set<Entity> &entities() const { return m_activeEntities; }
};
