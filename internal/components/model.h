#pragma once
// Model component referencing a mesh and its materials.

#include <vector>

struct Model {
  uint32_t meshHandle{0};
  std::vector<uint32_t> materialHandles{};

  Model() = default;
  Model(uint32_t mesh, std::vector<uint32_t> materials) : meshHandle(mesh), materialHandles(std::move(materials)) {}
};