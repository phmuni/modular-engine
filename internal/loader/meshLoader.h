#pragma once
#include "model/mesh.h"
#include <iostream> // IWYU pragma: keep
#include <memory>
#include <string>
#include <tiny_obj_loader/tiny_obj_loader.h>

class MeshLoader {
public:
  static std::shared_ptr<Mesh> loadFromOBJ(const std::string &filename);
};
