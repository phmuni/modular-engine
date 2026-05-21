
// Mesh class implementation with OBJ loading using tinyobjloader and vertex caching for optimization.
#define TINYOBJLOADER_IMPLEMENTATION

#include "rendering/resources/mesh.h"
#include "foundation/core/config.h"
#include <filesystem>
#include <iostream>
#include <tiny_obj_loader/tiny_obj_loader.h>

namespace std {
template <> struct hash<Vertex> {
  size_t operator()(const Vertex &v) const {
    size_t h = 0;
    auto combine = [&](float f) { h ^= std::hash<float>{}(f) + 0x9e3779b9 + (h << 6) + (h >> 2); };
    combine(v.position.x);
    combine(v.position.y);
    combine(v.position.z);
    combine(v.normal.x);
    combine(v.normal.y);
    combine(v.normal.z);
    combine(v.texCoord.x);
    combine(v.texCoord.y);
    return h;
  }
};
} // namespace std

Mesh::Mesh(std::string filename) {
  const std::string originalFilename = filename;
  if (!loadOBJ(std::move(filename)))
    std::cerr << "[Mesh] Failed to load: " << originalFilename << '\n';
}

Mesh::~Mesh() {
  glDeleteVertexArrays(1, &m_VAO);
  glDeleteBuffers(1, &m_VBO);
  glDeleteBuffers(1, &m_EBO);
}

void Mesh::setupBuffers(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices) {
  if (vertices.empty() || indices.empty()) {
    std::cerr << "[Mesh] No vertices or indices to setup\n";
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

bool Mesh::loadOBJ(std::string filename) {
  std::string normalized = PathUtils::normalizeSeparators(std::move(filename));

  if (!PathUtils::hasExtension(normalized) || !std::filesystem::exists(normalized)) {
    std::string withObj = std::string(PathUtils::stripExtension(normalized)) + ".obj";
    if (std::filesystem::exists(withObj))
      normalized = std::move(withObj);
  }

  auto slash = normalized.rfind('/');
  m_baseDir = (slash != std::string::npos) ? normalized.substr(0, slash + 1) : "./";

  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;

  bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, normalized.c_str(), m_baseDir.c_str());

  if (!err.empty())
    std::cerr << "[Mesh] " << err << '\n';

  if (!success)
    return false;

  m_mtlMaterials.reserve(materials.size());
  for (const auto &mat : materials) {
    MtlMaterialData mtl;
    mtl.name = mat.name;
    mtl.diffuseTexPath = mat.diffuse_texname;
    mtl.specularTexPath = mat.specular_texname;
    mtl.normalTexPath = mat.normal_texname.empty() ? mat.bump_texname : mat.normal_texname;
    mtl.emissionTexPath = mat.emissive_texname;
    mtl.diffuseColor = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]};
    mtl.specularColor = {mat.specular[0], mat.specular[1], mat.specular[2]};
    mtl.emissionColor = {mat.emission[0], mat.emission[1], mat.emission[2]};
    mtl.shininess = mat.shininess > 0.0f ? mat.shininess : 16.0f;
    m_mtlMaterials.push_back(std::move(mtl));
  }

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::unordered_map<Vertex, uint32_t> vertexCache;
  const bool generateNormals = attrib.normals.empty();

  for (const auto &shape : shapes) {
    const size_t numFaces = shape.mesh.num_face_vertices.size();

    std::vector<size_t> faceIndexOffset(numFaces);
    if (numFaces > 0) {
      faceIndexOffset[0] = 0;
      for (size_t f = 1; f < numFaces; f++)
        faceIndexOffset[f] = faceIndexOffset[f - 1] + shape.mesh.num_face_vertices[f - 1];
    }

    std::unordered_map<int, std::vector<size_t>> matFaceGroups;
    for (size_t f = 0; f < numFaces; f++) {
      int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;
      matFaceGroups[matId].push_back(f);
    }

    auto emitVertex = [&](const tinyobj::index_t &idx) {
      Vertex vertex{};

      if (idx.vertex_index >= 0) {
        vertex.position = {attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1],
                           attrib.vertices[3 * idx.vertex_index + 2]};
      }

      if (idx.normal_index >= 0) {
        vertex.normal = {attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1],
                         attrib.normals[3 * idx.normal_index + 2]};
      }

      if (idx.texcoord_index >= 0) {
        vertex.texCoord = {attrib.texcoords[2 * idx.texcoord_index + 0], attrib.texcoords[2 * idx.texcoord_index + 1]};
      }

      auto [it, inserted] = vertexCache.emplace(vertex, static_cast<uint32_t>(vertices.size()));
      if (inserted)
        vertices.push_back(vertex);
      indices.push_back(it->second);
    };

    for (auto &[matId, faceIndices] : matFaceGroups) {
      uint32_t submeshStart = static_cast<uint32_t>(indices.size());

      for (size_t f : faceIndices) {
        size_t fv = shape.mesh.num_face_vertices[f];
        size_t offset = faceIndexOffset[f];

        if (fv < 3)
          continue;

        const auto &first = shape.mesh.indices[offset + 0];
        for (size_t v = 1; v + 1 < fv; ++v) {
          emitVertex(first);
          emitVertex(shape.mesh.indices[offset + v]);
          emitVertex(shape.mesh.indices[offset + v + 1]);
        }
      }

      uint32_t submeshCount = static_cast<uint32_t>(indices.size()) - submeshStart;
      m_submeshes.push_back({submeshStart, submeshCount, matId});
    }
  }

  if (generateNormals) {
    for (auto &vertex : vertices) {
      vertex.normal = glm::vec3(0.0f);
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      Vertex &a = vertices[indices[i]];
      Vertex &b = vertices[indices[i + 1]];
      Vertex &c = vertices[indices[i + 2]];

      glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
      float normalLength = glm::length(faceNormal);
      if (normalLength <= 0.0f)
        continue;

      faceNormal /= normalLength;
      a.normal += faceNormal;
      b.normal += faceNormal;
      c.normal += faceNormal;
    }

    for (auto &vertex : vertices) {
      float normalLength = glm::length(vertex.normal);
      if (normalLength > 0.0f) {
        vertex.normal /= normalLength;
      } else {
        vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
      }
    }
  }

  setupBuffers(vertices, indices);
  return true;
}