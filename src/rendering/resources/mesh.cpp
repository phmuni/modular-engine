// Mesh class core: buffer management and loader dispatch by file extension.

#include "rendering/resources/mesh.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace {

std::string toLowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}


} // namespace

Mesh::Mesh(std::string filename) {
  const std::string originalFilename = filename;
  std::string normalized = PathUtils::normalizeSeparators(std::move(filename));
  std::string extension = toLowerCopy(std::filesystem::path(normalized).extension().string());

  bool loaded = false;
  if (extension == ".glb" || extension == ".gltf") {
    loaded = loadGLB(std::move(normalized));
  } else if (extension == ".obj") {
    loaded = loadOBJ(std::move(normalized));
  } else {
    loaded = loadOBJ(normalized) || loadGLB(std::move(normalized));
  }

  if (!loaded)
    LOG_E("[Mesh] Failed to load: %s", originalFilename.c_str());
}

Mesh::~Mesh() {
  resetMesh();
}

void Mesh::resetMesh() {
  m_submeshes.clear();
  m_mtlMaterials.clear();

  if (m_VAO != 0) {
    glDeleteVertexArrays(1, &m_VAO);
    m_VAO = 0;
  }
  if (m_VBO != 0) {
    glDeleteBuffers(1, &m_VBO);
    m_VBO = 0;
  }
  if (m_EBO != 0) {
    glDeleteBuffers(1, &m_EBO);
    m_EBO = 0;
  }
}

void Mesh::setupBuffers(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices) {
  if (vertices.empty() || indices.empty()) {
    LOG_W("[Mesh] No vertices or indices to setup");
    return;
  }

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  glGenBuffers(1, &m_VBO);
  glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

  glGenBuffers(1, &m_EBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
}
