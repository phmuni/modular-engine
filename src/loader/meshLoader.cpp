#define TINYOBJLOADER_IMPLEMENTATION
#include "loader/meshLoader.h"

std::unique_ptr<Mesh> MeshLoader::loadFromOBJ(const std::string &filename) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;

  std::string baseDir = filename.substr(0, filename.find_last_of("/\\") + 1);
  bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filename.c_str(), baseDir.c_str());

  if (!err.empty())
    std::cerr << "[MeshLoader] Error: " << err << std::endl;
  if (!success)
    return nullptr;

  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  // For each face of each shape, we always create a new Vertex
  for (const auto &shape : shapes) {
    for (const auto &idx : shape.mesh.indices) {
      Vertex vertex{};

      vertex.position = {attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1],
                         attrib.vertices[3 * idx.vertex_index + 2]};

      // Normal, If it exists
      if (idx.normal_index >= 0) {
        vertex.normal = {attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1],
                         attrib.normals[3 * idx.normal_index + 2]};
      }

      if (idx.texcoord_index >= 0) {
        vertex.texCoord = {attrib.texcoords[2 * idx.texcoord_index + 0],
                           1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]};
      }

      vertices.emplace_back(vertex);
      indices.emplace_back(static_cast<unsigned int>(vertices.size() - 1));
    }
  }

  auto mesh = std::make_unique<Mesh>();
  mesh->setVerticesIndices(vertices, indices);
  return mesh;
}
