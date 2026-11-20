#pragma once
// A prefab file an editor action wrote, for undo: what the file held before and after (nothing: it did not exist).
// Kept apart from sceneSerializer.h so engine.h can hold it without the JSON library.

#include <optional>
#include <string>

struct PrefabFileEdit {
  std::string path; // resolved, as written
  std::optional<std::string> before;
  std::optional<std::string> after;
};
