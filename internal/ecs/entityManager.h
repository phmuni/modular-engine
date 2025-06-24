#pragma once
#include <unordered_set>

using Entity = int;

class EntityManager {
private:
  Entity nextEntityID = 0;
  std::unordered_set<Entity> activeEntities;

public:
  // Creates a new entity and marks it as active
  Entity createEntity();

  // Removes the entity from the set of active entities
  void destroyEntity(Entity entity);

  // Checks if the entity is still active
  bool isAlive(Entity entity) const { return activeEntities.count(entity) > 0; }
};
