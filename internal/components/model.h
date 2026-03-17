#pragma once
// Model component holding mesh and material handles.

#include <cstdint>
#include <vector>

struct Model {
  uint32_t meshHandle;
  std::vector<uint32_t> materialHandles;

  Model(uint32_t mesh, std::vector<uint32_t> mats) : meshHandle(mesh), materialHandles(std::move(mats)) {}
};