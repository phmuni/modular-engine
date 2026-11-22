#pragma once
// Reflection probes at run time (see components/reflectionProbe.h): what each probe in the open scenes captured, the
// GL cube map array that holds all their reflections, and the per-draw choice of which probes light a thing.
//
// A probe's data is its pre-filtered reflections (the same kind of map as the sky's, ibl.h: kPrefilterSize with
// kPrefilterLevels mip levels, whatever resolution it was captured at) and its diffuse light as spherical harmonics
// (sphericalHarmonics.h). It comes from the probe's baked file, or when there is none from a capture made when the
// scene loads (RenderSystem does those: the capture draws the scene). All probes' reflections live in one
// GL_TEXTURE_CUBE_MAP_ARRAY, a cube per probe, so the lit shader needs one texture unit for any number of them; it is
// made again when probes come and go (a scene loads or unloads, one is added or deleted) from the data kept on the CPU
// (about 0.8 MB each). Probes are static: nothing here runs per frame but a walk over the probe components.
//
// The baked file is a small binary: a magic and version, the capture's resolution, a hash of what the bake depended
// on (position, size, resolution: when it differs from the probe's now the file is "stale"), the 9 SH coefficients, and
// the reflection cube's levels as RGB16F, level by level and face by face (+X -X +Y -Y +Z -Z).

#include "components/reflectionProbe.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "rendering/ibl.h"
#include "rendering/resources/shader.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// What a capture or a baked file holds for one probe
struct ProbeData {
  int resolution = 128; // the capture's face size
  uint64_t hash = 0;    // probeHash() of the probe when it was captured
  Sh9 sh{};
  std::vector<uint16_t> texels; // RGB16F: texelCount() halves

  bool valid() const { return texels.size() == texelCount(); }
  // Halves in one probe's cube: 3 channels, 6 faces, every mip level
  static size_t texelCount();
};

// What a bake depends on: the probe's position, box and resolution (not its blend, intensity or box projection, which
// apply when drawing)
uint64_t probeHash(const glm::vec3 &position, const ReflectionProbe &probe);

enum class ProbeFileResult { Ok, Missing, Invalid };
bool writeProbeFile(const std::string &path, const ProbeData &data);
// `why` says what was wrong with an Invalid file (a bad magic or version, a short file)
ProbeFileResult readProbeFile(const std::string &path, ProbeData &data, std::string &why);

class ReflectionProbes {
public:
  static constexpr int kMaxPerDraw = 4;
  static constexpr uint32_t kNone = 0xFFFFFFFFu; // a probe selection with no probe in it

  // Makes the probes follow the scene's ReflectionProbe components: new ones read their baked file (or are queued for a
  // capture, see takePendingCaptures), gone ones are forgotten, and the cube map array is made again when the set that
  // has data changed. Needs the GL context. Cheap when nothing changed.
  void update(ComponentManager &componentManager);
  // The probes with no data yet (no file, or an unreadable one); each is handed over once (RenderSystem captures them
  // and calls setData)
  std::vector<Entity> takePendingCaptures();
  // A capture's result (or a re-baked file's): `source` is the baked path it stands for (empty: none)
  void setData(Entity entity, ProbeData data, const std::string &source, bool captured);
  // The data of a probe (null if it has none yet)
  const ProbeData *data(Entity entity) const;
  // Whether its data is of another position, box or resolution than the probe has now
  bool isStale(Entity entity) const;
  // Forget everything (the GL objects too)
  void shutdown();

  // The cube map array to bind (a 1x1 black one stands in when no probe has data, so the sampler is never left with
  // another kind of texture)
  GLuint texture();
  // Probes in the array: those with data that are not hidden
  int layerCount() const { return static_cast<int>(m_active.size()); }
  // How many probes were captured, and how many were read from files, since the start
  int captureCount() const { return m_captures; }
  int fileLoadCount() const { return m_fileLoads; }
  // How long the last file read took (ms)
  double lastLoadMs() const { return m_loadMs; }
  // How many times the array was made again
  int uploadCount() const { return m_uploads; }
  bool empty() const { return m_active.empty(); }

  // The probes whose box a sphere overlaps (a thing's world bounds), smallest box first, up to kMaxPerDraw: indices into
  // the array packed one per byte, kNone for none. Equal results compare equal, so callers can skip setting the same
  // uniforms again.
  uint32_t select(const glm::vec3 &center, float radius) const;
  // Sets the lit shader's probe uniforms (probeCount and the rest) for a selection
  void apply(const Shader &shader, uint32_t selection) const;

  // Reads the pre-filtered cube (GL_TEXTURE_CUBE_MAP, kPrefilterSize with kPrefilterLevels levels) back as RGB16F
  static std::vector<uint16_t> readCube(GLuint cube);

private:
  struct Slot {
    ProbeData data;
    bool hasData = false;
    bool active = true;       // not hidden
    bool queued = false;      // handed to the capture queue
    std::string sourcePath;   // the baked path the data stands for (what bakedPath said when it was read or captured)
    std::string triedPath;    // a path that was read and failed: not tried again until bakedPath changes
    uint64_t revision = 0;    // changes when the data does
    uint64_t currentHash = 0; // probeHash of the probe now
    // The component's values, copied each update
    glm::vec3 center{0.0f}, lo{0.0f}, hi{0.0f};
    float blend = 0.0f, intensity = 1.0f;
    bool boxProjection = true;
  };
  // One entry per probe in the array, in layer order
  struct Active {
    Entity entity;
    uint64_t revision;
    const Slot *slot;
    float volume;
  };

  std::map<Entity, Slot> m_slots; // by entity (ascending: the array's layer order)
  std::vector<Active> m_active;
  std::vector<Entity> m_pending;
  GLuint m_array = 0;
  std::vector<std::pair<Entity, uint64_t>> m_uploaded; // what is in the array: entity and revision per layer
  GLuint m_empty = 0;
  int m_captures = 0;
  int m_fileLoads = 0;
  int m_uploads = 0;
  double m_loadMs = 0.0;

  void rebuildArray();
  void uploadLayer(int layer, const ProbeData &data);
};
