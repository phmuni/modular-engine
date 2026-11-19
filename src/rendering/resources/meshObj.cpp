// Mesh OBJ loading using tinyobjloader.

#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/resources/mesh.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <tiny_obj_loader/tiny_obj_loader.h>
#include <unordered_map>

namespace {
// A face corner as the file writes it: position/texcoord/normal indices. Corners that repeat the same triple become
// one vertex; comparing three ints is much cheaper than hashing the eight floats they point to.
struct CornerKey {
  int position, normal, texCoord;
  bool operator==(const CornerKey &other) const {
    return position == other.position && normal == other.normal && texCoord == other.texCoord;
  }
};

struct CornerKeyHash {
  size_t operator()(const CornerKey &key) const {
    uint64_t h = static_cast<uint32_t>(key.position) * 0x9E3779B97F4A7C15ull;
    h ^= (static_cast<uint32_t>(key.normal) + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
    h ^= (static_cast<uint32_t>(key.texCoord) + 0x165667B19E3779F9ull) * 0xFF51AFD7ED558CCDull;
    return static_cast<size_t>(h ^ (h >> 29));
  }
};
} // namespace

bool Mesh::parseOBJ(std::string filename, MeshData &data) {

  std::string normalized = PathUtils::normalizeSeparators(std::move(filename));

  if (!PathUtils::hasExtension(normalized) || !std::filesystem::exists(normalized)) {
    std::string withObj = std::string(PathUtils::stripExtension(normalized)) + ".obj";
    if (std::filesystem::exists(withObj))
      normalized = std::move(withObj);
  }

  auto slash = normalized.rfind('/');
  data.baseDir = (slash != std::string::npos) ? normalized.substr(0, slash + 1) : "./";

  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;

  bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, normalized.c_str(), data.baseDir.c_str());

  if (!err.empty())
    LOG_W("[Mesh] %s", err.c_str());

  if (!success)
    return false;

  data.materials.reserve(materials.size());
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
    // d (dissolve) below 1 makes the material translucent
    if (mat.dissolve < 1.0f) {
      mtl.alphaMode = AlphaMode::Blend;
      mtl.baseColorFactor.a = mat.dissolve;
    }
    data.materials.push_back(std::move(mtl));
  }

  std::vector<Vertex> &vertices = data.vertices;
  std::vector<uint32_t> &indices = data.indices;
  std::unordered_map<CornerKey, uint32_t, CornerKeyHash> vertexCache;
  vertexCache.reserve(attrib.vertices.size() / 3 + attrib.vertices.size() / 6);

  std::vector<PendingPart> parts;

  for (size_t shapeIndex = 0; shapeIndex < shapes.size(); ++shapeIndex) {
    const auto &shape = shapes[shapeIndex];
    const size_t numFaces = shape.mesh.num_face_vertices.size();

    std::vector<size_t> faceIndexOffset(numFaces);
    if (numFaces > 0) {
      faceIndexOffset[0] = 0;
      for (size_t f = 1; f < numFaces; f++)
        faceIndexOffset[f] = faceIndexOffset[f - 1] + shape.mesh.num_face_vertices[f - 1];
    }

    // One part per material used by this object/group, in order of first use
    std::vector<int> shapeMaterials;
    std::vector<std::vector<size_t>> facesByMaterial;
    for (size_t f = 0; f < numFaces; f++) {
      int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;
      auto it = std::find(shapeMaterials.begin(), shapeMaterials.end(), matId);
      if (it == shapeMaterials.end()) {
        shapeMaterials.push_back(matId);
        facesByMaterial.emplace_back();
        it = shapeMaterials.end() - 1;
      }
      facesByMaterial[static_cast<size_t>(it - shapeMaterials.begin())].push_back(f);
    }

    auto emitVertex = [&](const tinyobj::index_t &idx, std::vector<uint32_t> &target) {
      const CornerKey key{idx.vertex_index, idx.normal_index, idx.texcoord_index};
      auto [cached, inserted] = vertexCache.try_emplace(key, static_cast<uint32_t>(vertices.size()));
      target.push_back(cached->second);
      if (!inserted)
        return;

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

      vertices.push_back(vertex);
    };

    const std::string shapeName = shape.name.empty() ? "part " + std::to_string(shapeIndex) : shape.name;
    for (size_t m = 0; m < shapeMaterials.size(); ++m) {
      PendingPart part;
      part.materialIndex = shapeMaterials[m];
      part.name = shapeName;
      if (shapeMaterials.size() > 1) {
        const int matId = shapeMaterials[m];
        part.name += "/" + (matId >= 0 && matId < static_cast<int>(materials.size()) ? materials[matId].name
                                                                                     : std::string("default"));
      }

      for (size_t f : facesByMaterial[m]) {
        size_t fv = shape.mesh.num_face_vertices[f];
        size_t offset = faceIndexOffset[f];

        if (fv < 3)
          continue;

        const auto &first = shape.mesh.indices[offset + 0];
        for (size_t v = 1; v + 1 < fv; ++v) {
          emitVertex(first, part.indices);
          emitVertex(shape.mesh.indices[offset + v], part.indices);
          emitVertex(shape.mesh.indices[offset + v + 1], part.indices);
        }
      }

      if (!part.indices.empty())
        parts.push_back(std::move(part));
    }
  }

  layoutParts(parts, data);

  // Faces without normals get smooth normals from the faces around them. Checked per vertex, not per file:
  // an OBJ can give normals to some faces only, and those vertices would otherwise stay (0, 0, 0) and render unlit.
  std::vector<glm::vec3> accumulated(vertices.size(), glm::vec3(0.0f));
  bool missingNormals = false;
  for (const auto &vertex : vertices)
    missingNormals = missingNormals || glm::dot(vertex.normal, vertex.normal) == 0.0f;

  if (missingNormals) {
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      const uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
      glm::vec3 faceNormal = glm::cross(vertices[ib].position - vertices[ia].position,
                                        vertices[ic].position - vertices[ia].position);
      float normalLength = glm::length(faceNormal);
      if (normalLength <= 0.0f)
        continue;
      faceNormal /= normalLength;
      accumulated[ia] += faceNormal;
      accumulated[ib] += faceNormal;
      accumulated[ic] += faceNormal;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
      if (glm::dot(vertices[i].normal, vertices[i].normal) > 0.0f)
        continue;
      float normalLength = glm::length(accumulated[i]);
      vertices[i].normal = normalLength > 0.0f ? accumulated[i] / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);
    }
  }

  return true;
}
