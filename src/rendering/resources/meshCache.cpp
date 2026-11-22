// Mesh cache entries: a header that identifies the source files, then the arrays exactly as MeshData holds them.

#include "rendering/resources/meshCache.h"

#include "foundation/core/fileCache.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <cstring>

namespace {
namespace fs = std::filesystem;

constexpr char kMagic[8] = {'M', 'E', 'S', 'H', 'C', 'A', 'C', 'H'};
// Bump when the layout below or what a loader produces changes, so old entries are parsed again
constexpr uint32_t kVersion = 4; // 4: PBR materials (metallic, roughness, occlusion) instead of specular and shininess
// The cache is binary-identical only for this layout of Vertex
constexpr uint32_t kVertexSize = sizeof(Vertex);

// What the entry was built from: the source file and the newest file next to it that it reads (.mtl materials,
// .bin glTF buffers)
struct Stamp {
  FileCache::FileStamp source;
  int64_t materialsTime = 0;

  bool operator==(const Stamp &other) const {
    return source == other.source && materialsTime == other.materialsTime;
  }
};

bool stampOf(const std::string &sourcePath, Stamp &stamp) {
  if (!FileCache::stampOf(sourcePath, stamp.source))
    return false;
  std::error_code error;
  stamp.materialsTime = 0;
  for (const auto &entry : fs::directory_iterator(fs::path(sourcePath).parent_path(), error))
    if (entry.path().extension() == ".mtl" || entry.path().extension() == ".bin")
      stamp.materialsTime = std::max(stamp.materialsTime, FileCache::timeOf(entry.last_write_time(error)));
  return true;
}

std::string entryPath(const std::string &sourcePath) {
  return FileCache::entryPath("meshes", FileCache::canonicalKey(sourcePath), ".mesh");
}

// Material fields that are plain values, in one block
struct MaterialValues {
  int32_t diffuseImage, normalImage, emissionImage, metallicRoughnessImage, occlusionImage;
  glm::vec3 diffuseColor, emissionColor;
  float metallic, roughness, occlusionStrength, emissionStrength;
  glm::vec4 baseColorFactor;
  int32_t alphaMode;
  float alphaCutoff;
  uint8_t doubleSided, unlit;
};
} // namespace

namespace MeshCache {

bool read(const std::string &sourcePath, MeshData &data) {
  Stamp current;
  if (!stampOf(sourcePath, current))
    return false;
  FileCache::Reader reader(entryPath(sourcePath));
  if (!reader)
    return false;

  char magic[8];
  uint32_t version = 0, vertexSize = 0;
  Stamp stored;
  if (!reader.value(magic) || std::memcmp(magic, kMagic, sizeof(magic)) != 0 || !reader.value(version) ||
      version != kVersion || !reader.value(vertexSize) || vertexSize != kVertexSize || !reader.value(stored) ||
      !(stored == current))
    return false;

  MeshData loaded;
  bool ok = reader.array(loaded.vertices) && reader.array(loaded.indices);

  uint64_t submeshCount = 0;
  ok = ok && reader.value(submeshCount);
  for (uint64_t i = 0; ok && i < submeshCount; ++i) {
    Submesh submesh{};
    int32_t material = -1;
    ok = reader.value(submesh.indexStart) && reader.value(submesh.indexCount) && reader.value(material) &&
         reader.string(submesh.name);
    submesh.materialIndex = material;
    loaded.submeshes.push_back(std::move(submesh));
  }

  // Embedded images (glTF): only how many and their kinds; the pixels are in the texture cache
  std::vector<uint8_t> imageKinds;
  ok = ok && reader.array(imageKinds);
  loaded.images.resize(imageKinds.size());
  for (size_t i = 0; i < imageKinds.size(); ++i)
    loaded.images[i].kind = static_cast<TextureKind>(imageKinds[i]);

  uint64_t materialCount = 0;
  ok = ok && reader.value(materialCount);
  for (uint64_t i = 0; ok && i < materialCount; ++i) {
    MtlMaterialData material;
    MaterialValues v{};
    ok = reader.string(material.name) && reader.string(material.diffuseTexPath) &&
         reader.string(material.normalTexPath) && reader.string(material.emissionTexPath) &&
         reader.string(material.metallicRoughnessTexPath) && reader.string(material.occlusionTexPath) &&
         reader.value(v);
    material.diffuseImage = v.diffuseImage;
    material.normalImage = v.normalImage;
    material.emissionImage = v.emissionImage;
    material.metallicRoughnessImage = v.metallicRoughnessImage;
    material.occlusionImage = v.occlusionImage;
    material.diffuseColor = v.diffuseColor;
    material.emissionColor = v.emissionColor;
    material.metallic = v.metallic;
    material.roughness = v.roughness;
    material.occlusionStrength = v.occlusionStrength;
    material.emissionStrength = v.emissionStrength;
    material.baseColorFactor = v.baseColorFactor;
    material.alphaMode = static_cast<AlphaMode>(v.alphaMode);
    material.alphaCutoff = v.alphaCutoff;
    material.doubleSided = v.doubleSided != 0;
    material.unlit = v.unlit != 0;
    loaded.materials.push_back(std::move(material));
  }

  if (!ok) {
    LOG_W("[MeshCache] Damaged entry for %s; parsing the file", sourcePath.c_str());
    return false;
  }
  data = std::move(loaded);
  return true;
}

void write(const std::string &sourcePath, const MeshData &data) {
  Stamp stamp;
  if (!stampOf(sourcePath, stamp))
    return;

  FileCache::Writer writer(entryPath(sourcePath));
  writer.value(kMagic);
  writer.value(kVersion);
  writer.value(kVertexSize);
  writer.value(stamp);
  writer.array(data.vertices);
  writer.array(data.indices);

  writer.value(static_cast<uint64_t>(data.submeshes.size()));
  for (const Submesh &submesh : data.submeshes) {
    writer.value(submesh.indexStart);
    writer.value(submesh.indexCount);
    writer.value(static_cast<int32_t>(submesh.materialIndex));
    writer.string(submesh.name);
  }

  std::vector<uint8_t> imageKinds;
  for (const DecodedImage &image : data.images)
    imageKinds.push_back(static_cast<uint8_t>(image.kind));
  writer.array(imageKinds);

  writer.value(static_cast<uint64_t>(data.materials.size()));
  for (const MtlMaterialData &m : data.materials) {
    writer.string(m.name);
    writer.string(m.diffuseTexPath);
    writer.string(m.normalTexPath);
    writer.string(m.emissionTexPath);
    writer.string(m.metallicRoughnessTexPath);
    writer.string(m.occlusionTexPath);
    MaterialValues v{};
    v.diffuseImage = m.diffuseImage;
    v.normalImage = m.normalImage;
    v.emissionImage = m.emissionImage;
    v.metallicRoughnessImage = m.metallicRoughnessImage;
    v.occlusionImage = m.occlusionImage;
    v.diffuseColor = m.diffuseColor;
    v.emissionColor = m.emissionColor;
    v.metallic = m.metallic;
    v.roughness = m.roughness;
    v.occlusionStrength = m.occlusionStrength;
    v.emissionStrength = m.emissionStrength;
    v.baseColorFactor = m.baseColorFactor;
    v.alphaMode = static_cast<int32_t>(m.alphaMode);
    v.alphaCutoff = m.alphaCutoff;
    v.doubleSided = static_cast<uint8_t>(m.doubleSided);
    v.unlit = static_cast<uint8_t>(m.unlit);
    writer.value(v);
  }

  if (!writer.commit())
    LOG_W("[MeshCache] Could not write the entry for %s", sourcePath.c_str());
}

} // namespace MeshCache
