#pragma once
// Model component referencing a mesh and its materials.

#include <vector>

struct Model {
  uint32_t meshHandle{0};
  std::vector<uint32_t> materialHandles{};
  float opacity{1.0f};

  Model() = default;
  Model(uint32_t mesh, std::vector<uint32_t> materials, float modelOpacity = 1.0f)
      : meshHandle(mesh), materialHandles(std::move(materials)), opacity(modelOpacity) {}
};