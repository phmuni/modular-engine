// Mesh class core: buffer management and loader dispatch by file extension.

#include "rendering/resources/mesh.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/resources/material.h"
#include "rendering/resources/meshCache.h"
#include "foundation/core/fileCache.h"
#include "rendering/resources/textureProcessing.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <future>

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
  auto baseDirOf = [](const std::string &path) {
    auto slash = path.rfind('/');
    return slash != std::string::npos ? path.substr(0, slash + 1) : std::string("./");
  };
  if (extension == ".glb" || extension == ".gltf") {
    // Like OBJ files, from the mesh cache after the first time, with the embedded images from the texture cache
    if (MeshCache::read(normalized, data) && loadCachedImages(normalized, data)) {
      data.baseDir = baseDirOf(normalized);
      data.fromCache = loaded = true;
    } else {
      data = MeshData{};
      loaded = parseGLB(normalized, data);
      if (loaded)
        MeshCache::write(normalized, data);
    }
  } else if (extension == ".obj") {
    // Parsing a large OBJ takes long; after the first time its result comes from the mesh cache
    if (MeshCache::read(normalized, data)) {
      data.baseDir = baseDirOf(normalized);
      data.fromCache = loaded = true;
    } else {
      loaded = parseOBJ(normalized, data);
      if (loaded)
        MeshCache::write(normalized, data);
    }
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
  data.boundsMin = boundsMin;
  data.boundsMax = boundsMax;
  buildCollisionGeometry(data);
  for (const auto &vertex : data.vertices)
    data.boundsRadius = glm::max(data.boundsRadius, glm::length(vertex.position - data.boundsCenter));

  // Texture files are decoded here too, so the main thread only uploads them; several at once when there are several
  std::vector<std::pair<std::string, TextureKind>> textures;
  auto collect = [&](const std::string &relativePath, int embeddedImage, TextureKind kind) {
    if (relativePath.empty() || embeddedImage >= 0)
      return;
    std::string path = PathUtils::normalizeSeparators(data.baseDir + relativePath);
    if (std::none_of(textures.begin(), textures.end(), [&](const auto &texture) { return texture.first == path; }))
      textures.emplace_back(std::move(path), kind);
  };
  for (const auto &material : data.materials) {
    collect(material.diffuseTexPath, material.diffuseImage, TextureKind::Color);
    collect(material.specularTexPath, material.specularImage, TextureKind::Color);
    collect(material.normalTexPath, material.normalImage, TextureKind::Normal);
    collect(material.emissionTexPath, material.emissionImage, TextureKind::Color);
  }
  std::vector<std::future<DecodedImage>> decoding;
  for (size_t i = 1; i < textures.size(); ++i)
    decoding.push_back(std::async(std::launch::async, [&texture = textures[i]] {
      return Material::decodeTexture(texture.first, texture.second);
    }));
  if (!textures.empty())
    data.externalImages.emplace(textures[0].first, Material::decodeTexture(textures[0].first, textures[0].second));
  for (size_t i = 1; i < textures.size(); ++i)
    data.externalImages.emplace(textures[i].first, decoding[i - 1].get());

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
      imageTextures[i] = Material::uploadTexture(std::move(data.images[i]));
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
  m_boundsMin = data.boundsMin;
  m_boundsMax = data.boundsMax;
  m_indexCount = static_cast<uint32_t>(data.indices.size());
  m_collisionVertices = std::move(data.collisionVertices);
  m_collisionIndices = std::move(data.collisionIndices);

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

  // Location 10: 3-9 hold the per-instance matrices (Renderer::InstanceData)
  glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, tangent));
  glEnableVertexAttribArray(10);

  glBindVertexArray(0);

  data.vertices = {};
  data.indices = {};
}

std::string Mesh::embeddedImageKey(const std::string &file, size_t image) {
  return FileCache::canonicalKey(file) + "#image" + std::to_string(image);
}

bool Mesh::loadCachedImages(const std::string &file, MeshData &data) {
  for (size_t i = 0; i < data.images.size(); ++i) {
    DecodedImage &image = data.images[i];
    if (!TextureProcessing::loadCached(embeddedImageKey(file, i), file, image.kind, image))
      return false;
  }
  return true;
}

void Mesh::generateTangents(MeshData &data) {
  std::vector<Vertex> &vertices = data.vertices;
  bool missing = false;
  for (const Vertex &vertex : vertices)
    missing = missing || vertex.tangent == glm::vec4(0.0f);
  if (!missing)
    return;

  // Per vertex, the sum of the +u (tangents) and +v (bitangents) directions of the triangles around it
  std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
  std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));
  for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
    const uint32_t ia = data.indices[i], ib = data.indices[i + 1], ic = data.indices[i + 2];
    const Vertex &a = vertices[ia], &b = vertices[ib], &c = vertices[ic];
    const glm::vec3 edge1 = b.position - a.position, edge2 = c.position - a.position;
    const glm::vec2 uv1 = b.texCoord - a.texCoord;
    const glm::vec2 uv2 = c.texCoord - a.texCoord;
    const float determinant = uv1.x * uv2.y - uv2.x * uv1.y;
    if (std::abs(determinant) < 1e-12f)
      continue; // no UV area: says nothing about directions
    const float r = 1.0f / determinant;
    const glm::vec3 tangent = (edge1 * uv2.y - edge2 * uv1.y) * r;
    const glm::vec3 bitangent = (edge2 * uv1.x - edge1 * uv2.x) * r;
    for (uint32_t index : {ia, ib, ic}) {
      tangents[index] += tangent;
      bitangents[index] += bitangent;
    }
  }

  for (size_t i = 0; i < vertices.size(); ++i) {
    Vertex &vertex = vertices[i];
    if (vertex.tangent != glm::vec4(0.0f))
      continue; // read from the file
    const glm::vec3 normal = vertex.normal;
    // Made perpendicular to the normal (Gram-Schmidt); without UVs, any perpendicular direction will do
    glm::vec3 tangent = tangents[i] - normal * glm::dot(normal, tangents[i]);
    if (glm::dot(tangent, tangent) < 1e-12f)
      tangent = glm::cross(normal, std::abs(normal.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
    tangent = glm::normalize(tangent);
    const float handedness = glm::dot(glm::cross(normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
    vertex.tangent = glm::vec4(tangent, handedness);
  }
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

void Mesh::buildCollisionGeometry(MeshData &data) {
  // Welds vertices that share a position, by their exact bits
  struct PositionHash {
    size_t operator()(const glm::vec3 &p) const {
      uint32_t bits[3];
      std::memcpy(bits, &p, sizeof(bits));
      return (static_cast<size_t>(bits[0]) * 73856093u) ^ (static_cast<size_t>(bits[1]) * 19349663u) ^
             (static_cast<size_t>(bits[2]) * 83492791u);
    }
  };
  std::unordered_map<glm::vec3, uint32_t, PositionHash> welded;
  welded.reserve(data.vertices.size());
  std::vector<uint32_t> remap(data.vertices.size());
  data.collisionVertices.clear();
  for (size_t i = 0; i < data.vertices.size(); ++i) {
    auto [it, added] = welded.try_emplace(data.vertices[i].position, static_cast<uint32_t>(data.collisionVertices.size()));
    if (added)
      data.collisionVertices.push_back(data.vertices[i].position);
    remap[i] = it->second;
  }
  data.collisionIndices.clear();
  data.collisionIndices.reserve(data.indices.size());
  for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
    const uint32_t a = remap[data.indices[i]], b = remap[data.indices[i + 1]], c = remap[data.indices[i + 2]];
    if (a != b && b != c && a != c) // degenerate triangles have no surface to hit
      data.collisionIndices.insert(data.collisionIndices.end(), {a, b, c});
  }
}

void Mesh::resetMesh() {
  m_collisionVertices.clear();
  m_collisionIndices.clear();
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
