
// Entity creation and destruction.

#include "foundation/ecs/entityManager.h"
#include <algorithm>

Entity EntityManager::createEntity() {
  Entity entity = m_nextEntity++;
  m_activeEntities.insert(entity);
  return entity;
}

void EntityManager::destroyEntity(Entity entity) { m_activeEntities.erase(entity); }

void EntityManager::restore(const EntityManager &saved) {
  const Entity next = std::max(m_nextEntity, saved.m_nextEntity);
  *this = saved;
  m_nextEntity = next;
}
