#define TINYOBJLOADER_IMPLEMENTATION
#include "rendering/resources/mesh.h"
#include <tiny_obj_loader/tiny_obj_loader.h>

// Construtor que carrega de arquivo OBJ
Mesh::Mesh(const std::string &filename) : VAO(0), VBO(0), EBO(0) {
  if (!loadFromOBJ(filename)) {
    std::cerr << "[Mesh] Failed to load OBJ file: " << filename << std::endl;
  }
}

// Construtor com vértices e índices
Mesh::Mesh(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices)
    : vertices(vertices), indices(indices), VAO(0), VBO(0), EBO(0) {
  setupBuffers();
}

Mesh::~Mesh() {
  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);
  glDeleteBuffers(1, &EBO);
}

void Mesh::setupBuffers() {
  if (vertices.empty() || indices.empty()) {
    std::cerr << "Erro: Vertices ou índices não definidos!" << std::endl;
    return;
  }

  glGenVertexArrays(1, &VAO);
  glBindVertexArray(VAO);

  glGenBuffers(1, &VBO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

  glGenBuffers(1, &EBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
}

GLuint Mesh::getVAO() const { return VAO; }

size_t Mesh::getIndexCount() const { return indices.size(); }

void Mesh::setVerticesIndices(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices) {
  this->vertices = vertices;
  this->indices = indices;
  setupBuffers();
}

bool Mesh::loadFromOBJ(const std::string &filename) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;
  std::string baseDir = filename.substr(0, filename.find_last_of("/\\") + 1);

  bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filename.c_str(), baseDir.c_str());

  if (!err.empty())
    std::cerr << "[Mesh] Error: " << err << std::endl;

  if (!success)
    return false;

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

  setupBuffers();
  return true;
}