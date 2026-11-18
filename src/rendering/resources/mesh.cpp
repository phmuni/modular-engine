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

void Mesh::layoutParts(std::vector<PendingPart> &parts, std::vector<uint32_t> &indices) {
  // Materials in order of first use, so the layout is deterministic and follows the file
  std::vector<int> materialOrder;
  for (const auto &part : parts)
    if (std::find(materialOrder.begin(), materialOrder.end(), part.materialIndex) == materialOrder.end())
      materialOrder.push_back(part.materialIndex);

  size_t total = 0;
  for (const auto &part : parts)
    total += part.indices.size();
  indices.clear();
  indices.reserve(total);

  m_submeshes.assign(parts.size(), Submesh{});
  for (int material : materialOrder) {
    for (size_t i = 0; i < parts.size(); ++i) {
      if (parts[i].materialIndex != material)
        continue;
      m_submeshes[i] = {static_cast<uint32_t>(indices.size()), static_cast<uint32_t>(parts[i].indices.size()),
                        parts[i].materialIndex, std::move(parts[i].name)};
      indices.insert(indices.end(), parts[i].indices.begin(), parts[i].indices.end());
    }
  }
}

int Mesh::findSubmesh(std::string_view name) const {
  for (size_t i = 0; i < m_submeshes.size(); ++i)
    if (m_submeshes[i].name == name)
      return static_cast<int>(i);
  return -1;
}

Mesh::~Mesh() {
  resetMesh();
}

void Mesh::resetMesh() {
  m_submeshes.clear();
  m_mtlMaterials.clear();
  m_drawOrder.clear();

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

  m_indexCount = static_cast<uint32_t>(indices.size());
  m_drawOrder.resize(m_submeshes.size());
  for (uint32_t i = 0; i < m_drawOrder.size(); ++i)
    m_drawOrder[i] = i;
  std::sort(m_drawOrder.begin(), m_drawOrder.end(),
            [&](uint32_t a, uint32_t b) { return m_submeshes[a].indexStart < m_submeshes[b].indexStart; });

  glm::vec3 boundsMin = vertices.front().position;
  glm::vec3 boundsMax = boundsMin;
  for (const auto &vertex : vertices) {
    boundsMin = glm::min(boundsMin, vertex.position);
    boundsMax = glm::max(boundsMax, vertex.position);
  }
  m_boundsCenter = (boundsMin + boundsMax) * 0.5f;
  m_boundsRadius = 0.0f;
  for (const auto &vertex : vertices)
    m_boundsRadius = glm::max(m_boundsRadius, glm::length(vertex.position - m_boundsCenter));

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
