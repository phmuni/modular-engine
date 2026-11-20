#pragma once
// Tags: a small set of labels on an entity ("enemy", "pickup") for finding groups of entities without a component of
// their own: Engine::findTagged, GameEntity::hasTag. Saved in scene files and prefabs as a JSON array of strings, and
// edited in the Properties panel. Case matters; an entity has each tag once.

#include <string>
#include <vector>

struct Tags {
  std::vector<std::string> tags;
};
