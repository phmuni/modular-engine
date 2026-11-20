#pragma once
// Links an instance of a prefab to its file, so editing the file updates the instances (see SceneSerializer, "Linked
// prefab instances"). PrefabInstance sits on the instance's root; PrefabPart on every entity that came from the file
// (the root too), holding its place in the file's entity list, which is how the structure is matched again later.
// Entities added to an instance afterwards have no PrefabPart: updates leave them alone.
//
// What the instance changed from the file (its overrides) is not stored here: it is the difference between the
// instance as it is and the file as it was when the instance last matched it, found when needed (saving a scene,
// Apply, an update). Only the path and the indices live in components, so they cost nothing per frame.

#include <string>

struct PrefabInstance {
  std::string path; // as scene files write paths: relative to the executable's folder ("../assets/prefabs/car.json")
};

struct PrefabPart {
  int index = 0; // position in the prefab file's "entities" list
};
