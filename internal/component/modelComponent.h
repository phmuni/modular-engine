#pragma once

#include "model/material.h"
#include "model/mesh.h"
#include <memory>

struct ModelComponent {
  std::shared_ptr<Mesh> mesh;
  std::shared_ptr<Material> material;

  ModelComponent(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material) : mesh(mesh), material(material) {}
};
