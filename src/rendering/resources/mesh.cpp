// Mesh class core: buffer management and loader dispatch by file extension.

#include "rendering/resources/mesh.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/resources/material.h"
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
  MeshData data = parse(std::move(filename));
  upload(data);
}

MeshData Mesh::parse(std::string filename) {
  MeshData data;
  const std::string originalFilename = filename;
  std::string normalized = PathUtils::normalizeSeparators(std::move(filename));
  std::string extension = toLowerCopy(std::filesystem::path(normalized).extension().string());

  bool loaded = false;
  if (extension == ".glb" || extension == ".gltf") {
    loaded = parseGLB(std::move(normalized), data);
  } else if (extension == ".obj") {
    loaded = parseOBJ(std::move(normalized), data);
  } else {
    loaded = parseOBJ(normalized, data);
    if (!loaded) {
      data = MeshData{};
      loaded = parseGLB(std::move(normalized), data);
    }
  }

  if (!loaded || data.vertices.empty() || data.indices.empty()) {
    LOG_E("[Mesh] Failed to load: %s", originalFilename.c_str());
    return MeshData{};
  }

  glm::vec3 boundsMin = data.vertices.front().position;
  glm::vec3 boundsMax = boundsMin;
  for (const auto &vertex : data.vertices) {
    boundsMin = glm::min(boundsMin, vertex.position);
    boundsMax = glm::max(boundsMax, vertex.position);
  }
  data.boundsCenter = (boundsMin + boundsMax) * 0.5f;
  for (const auto &vertex : data.vertices)
    data.boundsRadius = glm::max(data.boundsRadius, glm::length(vertex.position - data.boundsCenter));

  // Texture files are decoded here too, so the main thread only uploads them
  auto prefetch = [&](const std::string &relativePath, int embeddedImage) {
    if (relativePath.empty() || embeddedImage >= 0)
      return;
    std::string path = PathUtils::normalizeSeparators(data.baseDir + relativePath);
    if (data.externalImages.find(path) == data.externalImages.end())
      data.externalImages.emplace(path, Material::decodeTexture(path));
  };
  for (const auto &material : data.materials) {
    prefetch(material.diffuseTexPath, material.diffuseImage);
    prefetch(material.specularTexPath, material.specularImage);
    prefetch(material.normalTexPath, material.normalImage);
    prefetch(material.emissionTexPath, material.emissionImage);
  }

  data.loaded = true;
  return data;
}

void Mesh::upload(MeshData &data) {
  resetMesh();
  if (!data.loaded)
    return;

  // Embedded images become textures owned by this mesh's materials
  std::vector<GLuint> imageTextures(data.images.size(), 0);
  for (size_t i = 0; i < data.images.size(); ++i)
    if (!data.images[i].empty())
      imageTextures[i] = Material::uploadTexture(data.images[i]);
  auto textureFor = [&](int image) { return image >= 0 && image < static_cast<int>(imageTextures.size()) ? imageTextures[image] : 0; };
  for (auto &material : data.materials) {
    material.diffuseTextureId = textureFor(material.diffuseImage);
    material.specularTextureId = textureFor(material.specularImage);
    material.normalTextureId = textureFor(material.normalImage);
    material.emissionTextureId = textureFor(material.emissionImage);
  }
  data.images.clear();

  m_submeshes = std::move(data.submeshes);
  m_mtlMaterials = std::move(data.materials);
  m_baseDir = std::move(data.baseDir);
  m_boundsCenter = data.boundsCenter;
  m_boundsRadius = data.boundsRadius;
  m_indexCount = static_cast<uint32_t>(data.indices.size());

  m_drawOrder.resize(m_submeshes.size());
  for (uint32_t i = 0; i < m_drawOrder.size(); ++i)
    m_drawOrder[i] = i;
  std::sort(m_drawOrder.begin(), m_drawOrder.end(),
            [&](uint32_t a, uint32_t b) { return m_submeshes[a].indexStart < m_submeshes[b].indexStart; });

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  glGenBuffers(1, &m_VBO);
  glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
  glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(Vertex), data.vertices.data(), GL_STATIC_DRAW);

  glGenBuffers(1, &m_EBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(uint32_t), data.indices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);

  data.vertices = {};
  data.indices = {};
}

void Mesh::layoutParts(std::vector<PendingPart> &parts, MeshData &data) {
  // Materials in order of first use, so the layout is deterministic and follows the file
  std::vector<int> materialOrder;
  for (const auto &part : parts)
    if (std::find(materialOrder.begin(), materialOrder.end(), part.materialIndex) == materialOrder.end())
      materialOrder.push_back(part.materialIndex);

  size_t total = 0;
  for (const auto &part : parts)
    total += part.indices.size();
  data.indices.clear();
  data.indices.reserve(total);

  data.submeshes.assign(parts.size(), Submesh{});
  for (int material : materialOrder) {
    for (size_t i = 0; i < parts.size(); ++i) {
      if (parts[i].materialIndex != material)
        continue;
      data.submeshes[i] = {static_cast<uint32_t>(data.indices.size()), static_cast<uint32_t>(parts[i].indices.size()),
                           parts[i].materialIndex, std::move(parts[i].name)};
      data.indices.insert(data.indices.end(), parts[i].indices.begin(), parts[i].indices.end());
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
