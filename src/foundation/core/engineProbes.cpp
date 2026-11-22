// Engine: reflection probes, baking them to files and capturing them again (see components/reflectionProbe.h).

#include "foundation/core/engine.h"

#include "components/name.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/reflectionProbes.h"
#include "systems/renderSystem.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace {
// A name that is safe as a file name on every platform: letters, digits, '-' and '_'
std::string fileSafe(const std::string &name, const std::string &fallback) {
  std::string safe;
  for (unsigned char c : name)
    safe += (std::isalnum(c) || c == '-' || c == '_') ? static_cast<char>(c) : '_';
  return safe.empty() ? fallback : safe;
}

// A path the project chose itself: absolute, or relative to the executable like "../tests/..." (a plain path is
// relative to assets/, which bake fills in)
bool isExplicitPath(const std::string &path) {
  return std::filesystem::path(path).is_absolute() || path.compare(0, 3, "../") == 0;
}
} // namespace

// Where the probe's baked file goes: its own bakedPath when that is an absolute path or one starting with ../ (a project
// can put the files anywhere), else probes/<scene file name>/<entity name>.probe relative to assets/. Two probes of one scene with the same
// name are told apart by their entity numbers (the lower one keeps the plain name).
std::string Engine::reflectionProbeFile(Entity entity) {
  const ReflectionProbe &probe = m_componentManager.get<ReflectionProbe>(entity);
  if (isExplicitPath(probe.bakedPath))
    return probe.bakedPath;
  std::string stem = "untitled";
  const int scene = m_entityManager.sceneOf(entity);
  if (const EntityManager::Scene *open = m_entityManager.findScene(scene))
    if (!open->path.empty())
      stem = fileSafe(std::filesystem::path(open->path).stem().string(), "scene");
  auto nameOf = [&](Entity probeEntity) {
    const Name *name = m_componentManager.tryGet<Name>(probeEntity);
    return fileSafe(name ? name->name : std::string(), "probe");
  };
  std::string name = nameOf(entity);
  m_componentManager.forEachComponent<ReflectionProbe>([&](Entity other, ReflectionProbe &) {
    if (other < entity && m_entityManager.sceneOf(other) == scene && nameOf(other) == name)
      name = nameOf(entity) + "_" + std::to_string(entity);
  });
  return "probes/" + stem + "/" + name + ".probe";
}

bool Engine::captureReflectionProbe(Entity entity, bool write) {
  if (!isAlive(entity) || !m_componentManager.has<ReflectionProbe>(entity) || !m_componentManager.has<Transform>(entity))
    return false;
  if (write && isTesting()) {
    LOG_W("[Probes] Not baking while the game is testing in the editor: stop it first");
    return false;
  }
  auto &renderSystem = m_systemManager.getSystem<RenderSystem>();
  ReflectionProbes &probes = renderSystem.probes();
  probes.update(m_componentManager); // it may have been added since the last frame
  std::optional<ProbeData> data = renderSystem.captureProbe(m_systemManager, m_componentManager, entity);
  if (!data) {
    LOG_W("[Probes] Capturing the probe of entity %d failed", static_cast<int>(entity));
    return false;
  }
  ReflectionProbe &probe = m_componentManager.get<ReflectionProbe>(entity);
  LOG_I("[Probes] Captured entity %d at %d x %d in %.1f ms", static_cast<int>(entity), data->resolution,
        data->resolution, renderSystem.lastCaptureMs());
  std::string source = probe.bakedPath;
  if (write) {
    const std::string path = reflectionProbeFile(entity);
    if (!writeProbeFile(EngineConfig::resolveAsset(path), *data)) {
      LOG_E("[Probes] Cannot write %s", EngineConfig::resolveAsset(path).c_str());
      return false;
    }
    if (probe.bakedPath != path) {
      // The path is part of the scene (and of its undo history, like any edit of it)
      probe.bakedPath = path;
      m_dirtyValid = false;
    }
    source = path;
  }
  probes.setData(entity, std::move(*data), source, true);
  probes.update(m_componentManager);
  return true;
}

bool Engine::bakeReflectionProbe(Entity entity) {
  const bool undoable = m_playMode == PlayMode::Editing && !hasOpenCheckpoint();
  if (undoable)
    beginCheckpoint();
  const bool baked = captureReflectionProbe(entity, true);
  if (undoable)
    endCheckpoint();
  return baked;
}

int Engine::bakeReflectionProbes() {
  std::vector<Entity> all;
  m_componentManager.forEachComponent<ReflectionProbe>([&](Entity entity, ReflectionProbe &) { all.push_back(entity); });
  std::sort(all.begin(), all.end());
  const bool undoable = m_playMode == PlayMode::Editing && !hasOpenCheckpoint();
  if (undoable)
    beginCheckpoint();
  int baked = 0;
  for (Entity entity : all)
    if (captureReflectionProbe(entity, true))
      ++baked;
  if (undoable)
    endCheckpoint();
  LOG_I("[Probes] Baked %d of %d reflection probes", baked, static_cast<int>(all.size()));
  return baked;
}

bool Engine::refreshReflectionProbe(Entity entity) { return captureReflectionProbe(entity, false); }

bool Engine::isReflectionProbeStale(Entity entity) {
  ReflectionProbes &probes = m_systemManager.getSystem<RenderSystem>().probes();
  probes.update(m_componentManager); // so a probe moved this frame is compared as it is now
  return probes.isStale(entity);
}

int Engine::reflectionProbeCount() { return m_systemManager.getSystem<RenderSystem>().probes().layerCount(); }

int Engine::reflectionProbeCaptures() { return m_systemManager.getSystem<RenderSystem>().probes().captureCount(); }
