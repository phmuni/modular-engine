// Entity creation and destruction.
#include "foundation/ecs/entityManager.h"

Entity EntityManager::createEntity() {
  Entity entity = m_nextEntityID++;
  m_activeEntities.insert(entity);
  return entity;
}

void EntityManager::destroyEntity(Entity entity) { m_activeEntities.erase(entity); }
