// Reflection probes at run time: the baked files, the cube map array and the per-draw selection (see
// reflectionProbes.h).

#include "rendering/reflectionProbes.h"

#include "components/hidden.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
constexpr char kMagic[4] = {'M', 'E', 'P', 'B'};
constexpr uint32_t kVersion = 1;

struct FileHeader {
  char magic[4];
  uint32_t version;
  uint32_t resolution;
  uint32_t baseSize;
  uint32_t levels;
  uint32_t reserved;
  uint64_t hash;
  float sh[27];
  uint32_t padding;
};
// Written as it is in memory: every field is 4 or 8 bytes, so there is no padding to differ between compilers
static_assert(sizeof(FileHeader) == 144, "no implicit padding");

uint64_t hashBytes(const void *data, size_t size, uint64_t hash) {
  const auto *bytes = static_cast<const unsigned char *>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int levelSize(int level) { return std::max(Ibl::kPrefilterSize >> level, 1); }
} // namespace

size_t ProbeData::texelCount() {
  size_t texels = 0;
  for (int level = 0; level < Ibl::kPrefilterLevels; ++level)
    texels += static_cast<size_t>(levelSize(level)) * levelSize(level) * 6 * 3;
  return texels;
}

uint64_t probeHash(const glm::vec3 &position, const ReflectionProbe &probe) {
  uint64_t hash = 1469598103934665603ull;
  const float values[6] = {position.x, position.y, position.z, probe.size.x, probe.size.y, probe.size.z};
  hash = hashBytes(values, sizeof(values), hash);
  const int resolution = ReflectionProbe::clampResolution(probe.resolution);
  hash = hashBytes(&resolution, sizeof(resolution), hash);
  return hash;
}

bool writeProbeFile(const std::string &path, const ProbeData &data) {
  if (!data.valid())
    return false;
  std::error_code error;
  const std::filesystem::path file(path);
  if (file.has_parent_path())
    std::filesystem::create_directories(file.parent_path(), error);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out)
    return false;
  FileHeader header{};
  std::memcpy(header.magic, kMagic, 4);
  header.version = kVersion;
  header.resolution = static_cast<uint32_t>(data.resolution);
  header.baseSize = static_cast<uint32_t>(Ibl::kPrefilterSize);
  header.levels = static_cast<uint32_t>(Ibl::kPrefilterLevels);
  header.hash = data.hash;
  for (int i = 0; i < 9; ++i) {
    header.sh[i * 3] = data.sh[i].x;
    header.sh[i * 3 + 1] = data.sh[i].y;
    header.sh[i * 3 + 2] = data.sh[i].z;
  }
  out.write(reinterpret_cast<const char *>(&header), sizeof(header));
  out.write(reinterpret_cast<const char *>(data.texels.data()),
            static_cast<std::streamsize>(data.texels.size() * sizeof(uint16_t)));
  return static_cast<bool>(out);
}

ProbeFileResult readProbeFile(const std::string &path, ProbeData &data, std::string &why) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return ProbeFileResult::Missing;
  FileHeader header{};
  in.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!in || std::memcmp(header.magic, kMagic, 4) != 0) {
    why = "not a probe file";
    return ProbeFileResult::Invalid;
  }
  if (header.version != kVersion) {
    why = "version " + std::to_string(header.version) + ", this engine reads " + std::to_string(kVersion);
    return ProbeFileResult::Invalid;
  }
  if (header.baseSize != static_cast<uint32_t>(Ibl::kPrefilterSize) ||
      header.levels != static_cast<uint32_t>(Ibl::kPrefilterLevels)) {
    why = "made for other cube map sizes";
    return ProbeFileResult::Invalid;
  }
  ProbeData loaded;
  loaded.resolution = static_cast<int>(header.resolution);
  loaded.hash = header.hash;
  for (int i = 0; i < 9; ++i)
    loaded.sh[i] = glm::vec3(header.sh[i * 3], header.sh[i * 3 + 1], header.sh[i * 3 + 2]);
  loaded.texels.resize(ProbeData::texelCount());
  in.read(reinterpret_cast<char *>(loaded.texels.data()),
          static_cast<std::streamsize>(loaded.texels.size() * sizeof(uint16_t)));
  if (!in || in.gcount() != static_cast<std::streamsize>(loaded.texels.size() * sizeof(uint16_t))) {
    why = "the file is cut short";
    return ProbeFileResult::Invalid;
  }
  data = std::move(loaded);
  return ProbeFileResult::Ok;
}

std::vector<uint16_t> ReflectionProbes::readCube(GLuint cube) {
  std::vector<uint16_t> texels;
  texels.reserve(ProbeData::texelCount());
  GLint previous = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &previous);
  GLint alignment = 4;
  glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
  for (int level = 0; level < Ibl::kPrefilterLevels; ++level) {
    const size_t faceHalves = static_cast<size_t>(levelSize(level)) * levelSize(level) * 3;
    for (int face = 0; face < 6; ++face) {
      const size_t at = texels.size();
      texels.resize(at + faceHalves);
      glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGB, GL_HALF_FLOAT, texels.data() + at);
    }
  }
  glPixelStorei(GL_PACK_ALIGNMENT, alignment);
  glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(previous));
  return texels;
}

void ReflectionProbes::update(ComponentManager &componentManager) {
  // Probes in the scene now; a slot whose probe is gone is dropped below
  bool changed = false;
  std::vector<Entity> seen;
  componentManager.forEachComponent<ReflectionProbe>([&](Entity entity, ReflectionProbe &probe) {
    const Transform *transform = componentManager.tryGet<Transform>(entity);
    if (!transform)
      return;
    seen.push_back(entity);
    auto found = m_slots.find(entity);
    if (found == m_slots.end()) {
      found = m_slots.emplace(entity, Slot()).first;
      changed = true;
    }
    Slot &slot = found->second;
    const bool active = !componentManager.has<Hidden>(entity);
    if (slot.active != active)
      changed = true;
    slot.active = active;
    slot.center = transform->position;
    slot.hi = probe.size * 0.5f;
    slot.lo = slot.center - slot.hi;
    slot.hi = slot.center + slot.hi;
    slot.blend = std::max(probe.blendDistance, 0.0f);
    slot.intensity = std::max(probe.intensity, 0.0f);
    slot.boxProjection = probe.boxProjection;
    slot.currentHash = probeHash(transform->position, probe);

    // Its data: from the baked file when it names one that reads, else a capture is wanted. A path already tried and
    // failed (or a capture already asked for) is not tried again until bakedPath changes.
    if (slot.hasData && slot.sourcePath == probe.bakedPath)
      return;
    if (!probe.bakedPath.empty() && slot.triedPath != probe.bakedPath) {
      const auto start = std::chrono::steady_clock::now();
      ProbeData loaded;
      std::string why;
      const std::string file = EngineConfig::resolveAsset(probe.bakedPath);
      const ProbeFileResult result = readProbeFile(file, loaded, why);
      if (result == ProbeFileResult::Ok) {
        m_loadMs = millisecondsSince(start);
        slot.data = std::move(loaded);
        slot.hasData = true;
        slot.sourcePath = probe.bakedPath;
        slot.triedPath = probe.bakedPath;
        ++slot.revision;
        ++m_fileLoads;
        changed = true;
        LOG_I("[Probes] Read %s in %.2f ms", probe.bakedPath.c_str(), m_loadMs);
        return;
      }
      slot.triedPath = probe.bakedPath;
      if (result == ProbeFileResult::Missing)
        LOG_W("[Probes] The baked file %s is missing: capturing the probe when the scene loads instead",
              probe.bakedPath.c_str());
      else
        LOG_W("[Probes] Cannot use the baked file %s (%s): capturing the probe when the scene loads instead",
              probe.bakedPath.c_str(), why.c_str());
      slot.queued = false;
    }
    if (!slot.hasData && !slot.queued) {
      slot.queued = true;
      m_pending.push_back(entity);
      if (probe.bakedPath.empty())
        LOG_I("[Probes] Entity %d has a reflection probe that was never baked: capturing it when the scene loads",
              static_cast<int>(entity));
    }
  });

  for (auto it = m_slots.begin(); it != m_slots.end();) {
    if (std::find(seen.begin(), seen.end(), it->first) == seen.end()) {
      it = m_slots.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }

  // The array holds the probes that have data and are shown, in entity order
  std::vector<Active> active;
  for (const auto &[entity, slot] : m_slots)
    if (slot.hasData && slot.active)
      active.push_back({entity, slot.revision, &slot,
                        (slot.hi.x - slot.lo.x) * (slot.hi.y - slot.lo.y) * (slot.hi.z - slot.lo.z)});
  bool sameSet = active.size() == m_active.size();
  for (size_t i = 0; sameSet && i < active.size(); ++i)
    sameSet = active[i].entity == m_active[i].entity && active[i].revision == m_active[i].revision;
  m_active = std::move(active);
  if (!sameSet || changed)
    rebuildArray();
}

std::vector<Entity> ReflectionProbes::takePendingCaptures() {
  std::vector<Entity> pending;
  pending.swap(m_pending);
  // A probe removed since it was queued, or captured by other means meanwhile, is not captured
  pending.erase(std::remove_if(pending.begin(), pending.end(),
                               [&](Entity e) {
                                 const auto slot = m_slots.find(e);
                                 return slot == m_slots.end() || slot->second.hasData;
                               }),
                pending.end());
  return pending;
}

void ReflectionProbes::setData(Entity entity, ProbeData data, const std::string &source, bool captured) {
  auto found = m_slots.find(entity);
  if (found == m_slots.end())
    return;
  Slot &slot = found->second;
  slot.data = std::move(data);
  slot.hasData = true;
  slot.queued = false;
  slot.sourcePath = source;
  slot.triedPath = source;
  ++slot.revision;
  if (captured)
    ++m_captures;
}

const ProbeData *ReflectionProbes::data(Entity entity) const {
  const auto found = m_slots.find(entity);
  return found != m_slots.end() && found->second.hasData ? &found->second.data : nullptr;
}

bool ReflectionProbes::isStale(Entity entity) const {
  const auto found = m_slots.find(entity);
  return found != m_slots.end() && found->second.hasData && found->second.data.hash != found->second.currentHash;
}

void ReflectionProbes::shutdown() {
  if (m_array)
    glDeleteTextures(1, &m_array);
  if (m_empty)
    glDeleteTextures(1, &m_empty);
  m_array = m_empty = 0;
  m_slots.clear();
  m_active.clear();
  m_pending.clear();
  m_uploaded.clear();
}

GLuint ReflectionProbes::texture() {
  if (m_array)
    return m_array;
  if (!m_empty) {
    glGenTextures(1, &m_empty);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_empty);
    glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_RGBA16F, 1, 1, 6, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  return m_empty;
}

void ReflectionProbes::uploadLayer(int layer, const ProbeData &data) {
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_array);
  GLint alignment = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const uint16_t *source = data.texels.data();
  for (int level = 0; level < Ibl::kPrefilterLevels; ++level) {
    const int size = levelSize(level);
    const size_t faceHalves = static_cast<size_t>(size) * size * 3;
    for (int face = 0; face < 6; ++face) {
      glTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, level, 0, 0, layer * 6 + face, size, size, 1, GL_RGB, GL_HALF_FLOAT,
                      source);
      source += faceHalves;
    }
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
}

void ReflectionProbes::setSkyIrradiance(const std::vector<uint16_t> *texels, int revision) {
  const bool had = m_sky != nullptr;
  if ((texels != nullptr) == had && (!texels || revision == m_skyRevision))
    return;
  m_sky = texels;
  m_skyRevision = revision;
  rebuildArray();
}

void ReflectionProbes::uploadSky() {
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_array);
  GLint alignment = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const size_t faceHalves = static_cast<size_t>(Ibl::kIrradianceSize) * Ibl::kIrradianceSize * 3;
  for (int face = 0; face < 6; ++face)
    glTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, kSkyLevel, 0, 0, kSkyLayer * 6 + face, Ibl::kIrradianceSize,
                    Ibl::kIrradianceSize, 1, GL_RGB, GL_HALF_FLOAT, m_sky->data() + face * faceHalves);
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
  m_uploadedSkyRevision = m_skyRevision;
}

// Layer 0 is the sky's irradiance, layer i + 1 the i-th probe
void ReflectionProbes::rebuildArray() {
  GLint previous = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP_ARRAY, &previous);
  const bool sky = m_sky != nullptr && m_sky->size() >= static_cast<size_t>(Ibl::kIrradianceSize) * Ibl::kIrradianceSize * 18;
  const int cubes = static_cast<int>(m_active.size()) + 1;
  const bool needed = sky || !m_active.empty();
  // The same probes in the same layers: only the ones whose data changed go up again
  bool sameLayout = m_array != 0 && m_uploaded.size() == m_active.size();
  for (size_t i = 0; sameLayout && i < m_active.size(); ++i)
    sameLayout = m_uploaded[i].first == m_active[i].entity;
  if (!needed) {
    if (m_array) {
      glDeleteTextures(1, &m_array);
      m_array = 0;
    }
    m_uploaded.clear();
    m_uploadedSkyRevision = -1;
    ++m_uploads;
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, static_cast<GLuint>(previous));
    return;
  }
  if (!sameLayout) {
    if (m_array)
      glDeleteTextures(1, &m_array);
    glGenTextures(1, &m_array);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_array);
    for (int level = 0; level < Ibl::kPrefilterLevels; ++level) {
      const int size = levelSize(level);
      glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, level, GL_RGBA16F, size, size, cubes * 6, 0, GL_RGBA, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAX_LEVEL, Ibl::kPrefilterLevels - 1);
    m_uploaded.assign(m_active.size(), {Entity(-1), 0});
    for (size_t i = 0; i < m_active.size(); ++i)
      m_uploaded[i].first = m_active[i].entity;
    m_uploadedSkyRevision = -1;
  }
  if (sky && m_uploadedSkyRevision != m_skyRevision)
    uploadSky();
  for (size_t i = 0; i < m_active.size(); ++i) {
    if (sameLayout && m_uploaded[i].second == m_active[i].revision)
      continue;
    uploadLayer(static_cast<int>(i) + 1, m_active[i].slot->data);
    m_uploaded[i].second = m_active[i].revision;
  }
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, static_cast<GLuint>(previous));
  ++m_uploads;
}

uint32_t ReflectionProbes::select(const glm::vec3 &center, float radius) const {
  if (m_active.empty())
    return kNone;
  int picked[kMaxPerDraw];
  float volumes[kMaxPerDraw];
  int count = 0;
  for (size_t i = 0; i < m_active.size(); ++i) {
    const Slot &slot = *m_active[i].slot;
    // Sphere against box: the distance from the center to the box's nearest point
    const glm::vec3 nearest = glm::clamp(center, slot.lo, slot.hi) - center;
    if (glm::dot(nearest, nearest) > radius * radius)
      continue;
    const float volume = m_active[i].volume;
    // Keep the smallest kMaxPerDraw, in order
    int at = count;
    while (at > 0 && volumes[at - 1] > volume)
      --at;
    if (at >= kMaxPerDraw)
      continue;
    const int last = std::min(count, kMaxPerDraw - 1);
    for (int k = last; k > at; --k) {
      picked[k] = picked[k - 1];
      volumes[k] = volumes[k - 1];
    }
    picked[at] = static_cast<int>(i);
    volumes[at] = volume;
    count = std::min(count + 1, kMaxPerDraw);
  }
  uint32_t selection = kNone;
  for (int k = 0; k < count; ++k)
    selection = (selection & ~(0xFFu << (k * 8))) | (static_cast<uint32_t>(picked[k]) << (k * 8));
  return selection;
}

void ReflectionProbes::apply(const Shader &shader, uint32_t selection) const {
  glm::vec3 centers[kMaxPerDraw], lows[kMaxPerDraw], highs[kMaxPerDraw], harmonics[kMaxPerDraw * 9];
  glm::vec4 info[kMaxPerDraw];
  int count = 0;
  for (int k = 0; k < kMaxPerDraw; ++k) {
    const uint32_t index = (selection >> (k * 8)) & 0xFFu;
    if (index >= m_active.size())
      break;
    const Slot &slot = *m_active[index].slot;
    centers[count] = slot.center;
    lows[count] = slot.lo;
    highs[count] = slot.hi;
    info[count] = glm::vec4(slot.blend, slot.intensity, static_cast<float>(index + 1), slot.boxProjection ? 1.0f : 0.0f);
    for (int c = 0; c < 9; ++c)
      harmonics[count * 9 + c] = slot.data.sh[c];
    ++count;
  }
  shader.setInt("probeCount", count);
  if (count == 0)
    return;
  shader.setVec3Array("probeCenter", centers, count);
  shader.setVec3Array("probeMin", lows, count);
  shader.setVec3Array("probeMax", highs, count);
  shader.setVec4Array("probeInfo", info, count);
  shader.setVec3Array("probeSh", harmonics, count * 9);
}
