#pragma once
#include "ecs/componentManager.h"
#include <unordered_set>

using Entity = int;

class EntityManager {
private:
  Entity nextEntityID = 0;
  std::unordered_set<Entity> activeEntities;

public:
  Entity createEntity();

  void deleteEntity(Entity entity);

  bool isAlive(Entity entity) const { return activeEntities.count(entity) > 0; }
};
