
// Entity creation and destruction.

#include "foundation/ecs/entityManager.h"
#include <algorithm>

Entity EntityManager::createEntity() {
  Entity entity = m_nextEntity++;
  m_activeEntities.insert(entity);
  if (m_creatingRuntime)
    m_runtimeEntities.insert(entity);
  return entity;
}

void EntityManager::destroyEntity(Entity entity) {
  m_activeEntities.erase(entity);
  m_runtimeEntities.erase(entity);
}

void EntityManager::restore(const EntityManager &saved) {
  const Entity next = std::max(m_nextEntity, saved.m_nextEntity);
  const bool creatingRuntime = m_creatingRuntime;
  *this = saved;
  m_nextEntity = next;
  m_creatingRuntime = creatingRuntime;
}
