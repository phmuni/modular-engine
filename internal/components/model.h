#pragma once
// Model component referencing a mesh and its materials.

#include <vector>

struct Model {
  uint32_t meshHandle{0};
  std::vector<uint32_t> materialHandles{};
  bool transparent{false};
  float opacity{1.0f};

  Model() = default;
  Model(uint32_t mesh, std::vector<uint32_t> materials, bool isTransparent = false, float modelOpacity = 1.0f)
      : meshHandle(mesh), materialHandles(std::move(materials)), transparent(isTransparent), opacity(modelOpacity) {}

  bool isTransparent() const { return transparent || opacity < 0.95f; }
};