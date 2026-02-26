#define TINYOBJLOADER_IMPLEMENTATION
#include "rendering/resources/mesh.h"
#include "foundation/core/config.h"
#include <filesystem>
#include <iostream>
#include <tiny_obj_loader/tiny_obj_loader.h>

// Constructor: load from OBJ file
Mesh::Mesh(const std::string &filename) {
  if (!loadOBJ(filename)) {
    std::cerr << "[Mesh] Failed to load: " << filename << std::endl;
  }
}

// Constructor: direct vertex/index data
Mesh::Mesh(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
           const std::vector<Submesh> &submeshes)
    : m_vertices(vertices), m_indices(indices), m_submeshes(submeshes) {
  setupBuffers();
}

Mesh::~Mesh() {
  glDeleteVertexArrays(1, &m_VAO);
  glDeleteBuffers(1, &m_VBO);
  glDeleteBuffers(1, &m_EBO);
}

// Setup OpenGL buffers (VAO/VBO/EBO)
void Mesh::setupBuffers() {
  if (m_vertices.empty() || m_indices.empty()) {
    std::cerr << "[Mesh] No vertices or indices to setup\n";
    return;
  }

  // VAO
  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  // VBO
  glGenBuffers(1, &m_VBO);
  glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
  glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(Vertex), m_vertices.data(), GL_STATIC_DRAW);

  // EBO
  glGenBuffers(1, &m_EBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t), m_indices.data(), GL_STATIC_DRAW);

  // Vertex attributes: position, normal, texCoord
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
}

GLuint Mesh::getVAO() const { return m_VAO; }

const std::vector<Submesh> &Mesh::getSubmeshes() const { return m_submeshes; }

void Mesh::setVerticesIndices(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
                              const std::vector<Submesh> &submeshes) {
  m_vertices = vertices;
  m_indices = indices;
  m_submeshes = submeshes;
  setupBuffers();
}

// Load OBJ using tinyobjloader
bool Mesh::loadOBJ(const std::string &filename) {
  std::string normalized = PathUtils::normalize(filename);

  // If no extension or file doesn't exist, try appending .obj
  if (!PathUtils::hasExtension(normalized) || !std::filesystem::exists(normalized)) {
    std::string withObj = PathUtils::stripExtension(normalized) + ".obj";
    if (std::filesystem::exists(withObj)) {
      normalized = withObj;
    }
  }

  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;
  std::string baseDir = normalized.substr(0, normalized.find_last_of('/') + 1);

  bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, normalized.c_str(), baseDir.c_str());

  if (!err.empty())
    std::cerr << "[Mesh] " << err << std::endl;

  if (!success)
    return false;

  // Build vertices/indices per shape (submesh)
  for (const auto &shape : shapes) {
    uint32_t submeshStart = static_cast<uint32_t>(m_indices.size());

    for (const auto &idx : shape.mesh.indices) {
      Vertex vertex{};

      // Position
      vertex.position = {attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1],
                         attrib.vertices[3 * idx.vertex_index + 2]};

      // Normal
      if (idx.normal_index >= 0) {
        vertex.normal = {attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1],
                         attrib.normals[3 * idx.normal_index + 2]};
      }

      // TexCoord (flip Y for OpenGL)
      if (idx.texcoord_index >= 0) {
        vertex.texCoord = {attrib.texcoords[2 * idx.texcoord_index + 0],
                           1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]};
      }

      m_vertices.push_back(vertex);
      m_indices.push_back(static_cast<uint32_t>(m_vertices.size() - 1));
    }

    uint32_t submeshCount = static_cast<uint32_t>(m_indices.size()) - submeshStart;
    m_submeshes.push_back({submeshStart, submeshCount});
  }

  setupBuffers();
  return true;
}
