#include "ecs/entityManager.h"

Entity EntityManager::createEntity() {
  Entity entity = nextEntityID++;
  activeEntities.insert(entity);
  return entity;
}

void EntityManager::destroyEntity(Entity entity) { activeEntities.erase(entity); }
