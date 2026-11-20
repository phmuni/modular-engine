// Linked prefab instances: an instance remembers its prefab file, and follows it when the file changes while keeping
// what it changed itself. See the SceneSerializer header for the model.

#include "systems/sceneSerializer.h"

#include "components/componentJson.h"
#include "components/name.h"
#include "components/parent.h"
#include "components/prefabInstance.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "systems/lightSystem.h"
#include "systems/navigationSystem.h"
#include "systems/physicsSystem.h"
#include "systems/renderSystem.h"
#include "systems/sceneSystem.h"
#include "systems/transformSystem.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace {
// Parts hold everything but what the instance owns: its place (Transform, the root's parent) and the link itself
bool isInstanceOwned(const std::string &key) { return key == "Transform" || key == "PrefabInstance" || key == "PrefabPart"; }

// The same file spelled two ways must give the same key
std::string fileKey(const std::string &resolvedPath) {
  return std::filesystem::path(resolvedPath).lexically_normal().generic_string();
}

std::string keyOf(const PrefabInstance &instance) { return fileKey(EngineConfig::resolveAsset(instance.path)); }

// A part's number from its key in the parts object; -1 for anything else (a patch can add odd keys)
int partIndex(const std::string &key) {
  char *end = nullptr;
  const long value = std::strtol(key.c_str(), &end, 10);
  return end && *end == '\0' && !key.empty() && value >= 0 && value < 1000000 ? static_cast<int>(value) : -1;
}

const nlohmann::json &componentsOf(const nlohmann::json &part) {
  static const nlohmann::json none = nlohmann::json::object();
  return part.is_object() && part.contains("components") && part["components"].is_object() ? part["components"] : none;
}
} // namespace

void SceneSerializer::fillPrefabFile(PrefabFile &file) {
  file.parts = json::object();
  file.root = -1;
  int roots = 0;
  for (size_t i = 0; i < file.entities.size(); ++i) {
    const json &entry = file.entities[i];
    json part = json::object();
    if (entry.contains("name"))
      part["name"] = entry["name"];
    json components = json::object();
    if (entry.contains("components") && entry["components"].is_object())
      for (const auto &item : entry["components"].items())
        if (!isInstanceOwned(item.key()))
          components[item.key()] = item.value();
    if (!components.contains("Parent")) {
      file.root = static_cast<int>(i);
      ++roots;
    }
    // Written as the engine writes it, or every instance would differ from its file by spelling
    if (components.contains("Model") && components["Model"].contains("path") && components["Model"]["path"].is_string())
      components["Model"]["path"] =
          EngineConfig::relativePath(EngineConfig::resolveAsset(components["Model"]["path"].get<std::string>()));
    part["components"] = std::move(components);
    file.parts[std::to_string(i)] = std::move(part);
  }
  roundFloats(file.parts);
  if (roots != 1)
    file.root = -1;
}

const SceneSerializer::PrefabFile *SceneSerializer::cachedPrefabFile(const std::string &resolvedPath) const {
  const std::string key = fileKey(resolvedPath);
  auto it = m_prefabFiles.find(key);
  if (it != m_prefabFiles.end())
    return &it->second;
  // Not read yet (a scene saved before anything used it): what the instances have becomes the baseline
  std::optional<json> scene = readFile(key, false);
  if (!scene)
    return nullptr;
  std::error_code error;
  PrefabFile file;
  file.entities = std::move((*scene)["entities"]);
  file.time = std::filesystem::last_write_time(key, error);
  fillPrefabFile(file);
  return &m_prefabFiles.emplace(key, std::move(file)).first->second;
}

const SceneSerializer::PrefabFile *SceneSerializer::prefabFile(const std::string &resolvedPath, bool checkChanged) {
  const std::string key = fileKey(resolvedPath);
  auto it = m_prefabFiles.find(key);
  if (it == m_prefabFiles.end()) {
    std::optional<json> scene = readFile(key);
    if (!scene)
      return nullptr;
    std::error_code error;
    PrefabFile file;
    file.entities = std::move((*scene)["entities"]);
    file.time = std::filesystem::last_write_time(key, error);
    fillPrefabFile(file);
    return &m_prefabFiles.emplace(key, std::move(file)).first->second;
  }
  if (!checkChanged)
    return &it->second;

  // A spawn notices a file edited since it was read, so a hand-edited prefab is used without restarting
  std::error_code error;
  const auto time = std::filesystem::last_write_time(key, error);
  if (error) {
    it->second.exists = false;
    return &it->second;
  }
  it->second.exists = true;
  if (time != it->second.time) {
    if (std::optional<json> scene = readFile(key)) {
      PrefabFile fresh;
      fresh.entities = std::move((*scene)["entities"]);
      fresh.time = time;
      fillPrefabFile(fresh);
      updateInstances(key, std::move(fresh), -1);
    }
  }
  return &m_prefabFiles.find(key)->second;
}

std::vector<std::pair<int, Entity>> SceneSerializer::collectParts(Entity root) const {
  std::vector<std::pair<int, Entity>> parts;
  const PrefabPart *rootPart = m_componentManager.tryGet<PrefabPart>(root);
  parts.emplace_back(rootPart ? rootPart->index : 0, root);
  std::unordered_set<int> seen{parts.front().first};
  auto &transformSystem = m_systemManager.getSystem<TransformSystem>();
  std::vector<Entity> pending{root};
  while (!pending.empty() && parts.size() < 100000) {
    const Entity entity = pending.back();
    pending.pop_back();
    for (Entity child : transformSystem.getChildren(m_componentManager, entity)) {
      if (m_componentManager.has<PrefabInstance>(child))
        continue; // another instance, parented here: not part of this one
      if (const PrefabPart *part = m_componentManager.tryGet<PrefabPart>(child))
        if (seen.insert(part->index).second)
          parts.emplace_back(part->index, child);
      pending.push_back(child); // entities added to the instance can hold parts too
    }
  }
  return parts;
}

Entity SceneSerializer::instanceRoot(Entity entity) const {
  if (!m_componentManager.has<PrefabInstance>(entity) && !m_componentManager.has<PrefabPart>(entity))
    return -1; // an entity added to an instance is its own
  for (int guard = 0; guard < 256 && entity != -1; ++guard) {
    if (m_componentManager.has<PrefabInstance>(entity))
      return entity;
    const Parent *parent = m_componentManager.tryGet<Parent>(entity);
    entity = parent ? parent->entity : -1;
  }
  return -1;
}

std::vector<Entity> SceneSerializer::instancesOf(const std::string &resolvedPath) const {
  const std::string key = fileKey(resolvedPath);
  std::vector<Entity> instances;
  m_componentManager.forEachComponent<PrefabInstance>([&](Entity entity, PrefabInstance &instance) {
    if (keyOf(instance) == key)
      instances.push_back(entity);
  });
  std::sort(instances.begin(), instances.end());
  return instances;
}

bool SceneSerializer::prefabFileExists(Entity root) const {
  const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(root);
  std::error_code error;
  return instance && std::filesystem::exists(EngineConfig::resolveAsset(instance->path), error);
}

// One part as the prefab file would hold it (see fillPrefabFile): without what the instance owns
SceneSerializer::json SceneSerializer::livePart(Entity entity, Entity root, const std::map<Entity, int> &indexOf) const {
  json part = json::object();
  if (const Name *name = m_componentManager.tryGet<Name>(entity))
    part["name"] = name->name;
  json components = json::object();
  for (const Serializer &serializer : m_serializers) {
    if (isInstanceOwned(serializer.key))
      continue;
    try {
      if (std::optional<json> data = serializer.save(entity))
        components[serializer.key] = std::move(*data);
    } catch (const std::exception &error) {
      LOG_W("[Scene] Could not save %s of entity %d: %s", serializer.key.c_str(), entity, error.what());
    }
  }
  const Parent *parent = entity != root ? m_componentManager.tryGet<Parent>(entity) : nullptr;
  if (parent) {
    auto it = indexOf.find(parent->entity);
    components["Parent"] = {{"parent", it != indexOf.end() ? it->second : indexOf.at(root)}, {"local", parent->local}};
  }
  part["components"] = std::move(components);
  return part;
}

SceneSerializer::json SceneSerializer::liveParts(Entity root, const std::vector<std::pair<int, Entity>> &parts) const {
  std::map<Entity, int> indexOf;
  for (const auto &[index, entity] : parts)
    indexOf[entity] = index;
  json result = json::object();
  for (const auto &[index, entity] : parts)
    result[std::to_string(index)] = livePart(entity, root, indexOf);
  roundFloats(result);
  return result;
}

// The changes an instance made, as a JSON Patch from the file's parts to its own
SceneSerializer::json SceneSerializer::instanceOverridesPatch(Entity root) const {
  const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(root);
  if (!instance)
    return nullptr;
  const PrefabFile *file = cachedPrefabFile(EngineConfig::resolveAsset(instance->path));
  if (!file)
    return nullptr;
  return json::diff(file->parts, liveParts(root, collectParts(root)));
}

// Each change on its own, so one that no longer fits the file (its part is gone) does not stop the others
SceneSerializer::json SceneSerializer::applyOverrides(const json &base, const json &patch) {
  json result = base;
  if (!patch.is_array())
    return result;
  for (const json &operation : patch) {
    try {
      result = result.patch(json::array({operation}));
    } catch (const json::exception &) {
    }
  }
  return result;
}

std::vector<std::string> SceneSerializer::instanceOverrides(Entity root) {
  const json patch = instanceOverridesPatch(root);
  std::vector<std::string> paths;
  if (!patch.is_array())
    return paths;
  const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(root);
  const PrefabFile *file = cachedPrefabFile(EngineConfig::resolveAsset(instance->path));
  for (const json &operation : patch) {
    // "/3/components/Model/opacity": the part, then what changed in it
    std::vector<std::string> tokens;
    std::istringstream stream(operation.value("path", ""));
    for (std::string token; std::getline(stream, token, '/');)
      if (!token.empty())
        tokens.push_back(token);
    if (tokens.empty())
      continue;
    std::string label = "part " + tokens[0];
    if (file && file->parts.contains(tokens[0]) && file->parts[tokens[0]].contains("name"))
      label = file->parts[tokens[0]]["name"].get<std::string>();
    if (tokens.size() == 1)
      label += operation.value("op", "") == "remove" ? " (removed)" : " (added)";
    for (size_t i = (tokens.size() > 1 && tokens[1] == "components") ? 2 : 1; i < tokens.size(); ++i)
      label += "/" + tokens[i];
    if (std::find(paths.begin(), paths.end(), label) == paths.end())
      paths.push_back(std::move(label));
  }
  return paths;
}

void SceneSerializer::syncInstance(Entity root, const json &target) {
  auto &sceneSystem = m_systemManager.getSystem<SceneSystem>();
  auto &transformSystem = m_systemManager.getSystem<TransformSystem>();
  const std::vector<std::pair<int, Entity>> parts = collectParts(root);
  std::map<int, Entity> entityOf;
  for (const auto &[index, entity] : parts)
    entityOf[index] = entity;
  const json current = liveParts(root, parts);
  const int rootIndex = parts.front().first;
  bool changed = false;

  // Parts the file no longer has go; what was attached to them stays, on the root
  std::vector<Entity> gone;
  for (const auto &[index, entity] : parts)
    if (entity != root && !target.contains(std::to_string(index)))
      gone.push_back(entity);
  for (Entity entity : gone)
    for (Entity child : transformSystem.getChildren(m_componentManager, entity))
      if (std::find(gone.begin(), gone.end(), child) == gone.end())
        transformSystem.setParent(m_componentManager, child, root);
  for (Entity entity : gone) {
    if (m_entityManager.isAlive(entity))
      sceneSystem.destroyEntity(entity);
    changed = true;
  }
  for (auto it = entityOf.begin(); it != entityOf.end();)
    it = (it->second != root && !target.contains(std::to_string(it->first))) ? entityOf.erase(it) : std::next(it);

  // Parts it has that the instance does not (new in the file, or removed by the instance and not by an override)
  std::vector<int> created;
  for (const auto &item : target.items()) {
    const int index = partIndex(item.key());
    if (index < 0 || entityOf.count(index))
      continue;
    const Entity entity = m_entityManager.createEntity();
    entityOf[index] = entity;
    created.push_back(index);
    if (item.value().contains("name"))
      m_componentManager.addInPlace<Name>(entity, item.value()["name"].get<std::string>());
    const json &components = componentsOf(item.value());
    for (const Serializer &serializer : m_serializers) {
      if (isInstanceOwned(serializer.key) || serializer.key == "Parent" || !components.contains(serializer.key))
        continue;
      try {
        serializer.load(entity, components[serializer.key]);
      } catch (const std::exception &error) {
        LOG_W("[Scene] Could not load %s of entity %d: %s", serializer.key.c_str(), entity, error.what());
      }
    }
    if (!m_componentManager.has<Transform>(entity))
      m_componentManager.addInPlace<Transform>(entity); // placed by its parent below
    m_componentManager.addInPlace<PrefabPart>(entity).index = index;
    changed = true;
  }

  // Parts both have: only what differs is touched, so nothing else (a body's state, a model's materials) is reset
  for (const auto &[index, entity] : entityOf) {
    if (std::find(created.begin(), created.end(), index) != created.end())
      continue;
    const std::string key = std::to_string(index);
    const json &want = target[key];
    const json &have = current[key];
    if (!want.is_object())
      continue;
    const bool wantName = want.contains("name"), haveName = have.contains("name");
    if (wantName && (!haveName || have["name"] != want["name"])) {
      m_componentManager.addInPlace<Name>(entity, want["name"].get<std::string>());
      changed = true;
    } else if (!wantName && haveName) {
      m_componentManager.removeComponent<Name>(entity);
      changed = true;
    }
    const json &wantComponents = componentsOf(want), &haveComponents = componentsOf(have);
    for (const Serializer &serializer : m_serializers) {
      if (isInstanceOwned(serializer.key) || serializer.key == "Parent")
        continue;
      const bool wanted = wantComponents.contains(serializer.key), had = haveComponents.contains(serializer.key);
      if (wanted && (!had || haveComponents[serializer.key] != wantComponents[serializer.key])) {
        if (had && serializer.remove)
          serializer.remove(entity);
        try {
          serializer.load(entity, wantComponents[serializer.key]);
        } catch (const std::exception &error) {
          LOG_W("[Scene] Could not load %s of entity %d: %s", serializer.key.c_str(), entity, error.what());
        }
        changed = true;
      } else if (!wanted && had) {
        if (serializer.remove)
          serializer.remove(entity);
        changed = true;
      }
    }
  }

  // Places, parents before their children: each part follows the parent the file gives it, at its local transform
  struct Place {
    int depth;
    int index;
  };
  std::vector<Place> places;
  for (const auto &item : target.items()) {
    const int index = partIndex(item.key());
    if (index < 0 || index == rootIndex || !entityOf.count(index) || !componentsOf(item.value()).contains("Parent"))
      continue;
    int depth = 0, at = index;
    while (at != rootIndex && depth < 64 && target.contains(std::to_string(at)) &&
           componentsOf(target[std::to_string(at)]).contains("Parent")) {
      at = componentsOf(target[std::to_string(at)])["Parent"].value("parent", rootIndex);
      ++depth;
    }
    places.push_back({depth, index});
  }
  std::sort(places.begin(), places.end(), [](const Place &a, const Place &b) { return a.depth < b.depth; });
  for (const Place &place : places) {
    const std::string key = std::to_string(place.index);
    const Entity entity = entityOf[place.index];
    const json &wantParent = componentsOf(target[key])["Parent"];
    const int parentIndex = wantParent.value("parent", rootIndex);
    const Entity parent = entityOf.count(parentIndex) ? entityOf[parentIndex] : root;
    const bool isNew = std::find(created.begin(), created.end(), place.index) != created.end();
    const bool sameLocal = !isNew && componentsOf(current[key]).contains("Parent") &&
                           componentsOf(current[key])["Parent"] == wantParent;
    if (sameLocal)
      continue;
    if (transformSystem.getParent(m_componentManager, entity) != parent)
      transformSystem.setParent(m_componentManager, entity, parent);
    transformSystem.setLocalTransform(m_componentManager, entity, wantParent.value("local", Transform()));
    changed = true;
  }

  if (!changed)
    return;
  // What undo and Stop do when they replace components: everything that read them starts again from them
  m_systemManager.getSystem<LightSystem>().rebuild(m_componentManager);
  m_systemManager.getSystem<RenderSystem>().markBatchesDirty();
  m_systemManager.getSystem<PhysicsSystem>().reset();
  m_systemManager.getSystem<NavigationSystem>().markDirty();
  transformSystem.updateHierarchy(m_componentManager);
}

size_t SceneSerializer::updateInstances(const std::string &resolvedPath, PrefabFile fresh, Entity skip) {
  const std::string key = fileKey(resolvedPath);
  std::vector<std::pair<Entity, json>> pending;
  auto old = m_prefabFiles.find(key);
  for (Entity instance : instancesOf(key)) {
    if (instance == skip)
      continue;
    // What it changed from the file as it was; measured before the file's new content replaces it
    json overrides = old != m_prefabFiles.end() ? json::diff(old->second.parts, liveParts(instance, collectParts(instance)))
                                                : json::array();
    pending.emplace_back(instance, std::move(overrides));
  }
  PrefabFile &stored = m_prefabFiles[key];
  stored = std::move(fresh);
  for (const auto &[instance, overrides] : pending)
    syncInstance(instance, applyOverrides(stored.parts, overrides));
  if (!pending.empty())
    LOG_I("[Scene] %s changed: %zu instance(s) updated", key.c_str(), pending.size());
  return pending.size();
}

void SceneSerializer::linkEntities(Entity root, const std::string &path, const std::vector<Entity> &entities) {
  for (size_t i = 0; i < entities.size(); ++i) {
    if (entities[i] == root)
      m_componentManager.addInPlace<PrefabInstance>(root).path = path;
    else
      m_componentManager.removeComponent<PrefabInstance>(entities[i]); // an instance inside is flattened
    m_componentManager.addInPlace<PrefabPart>(entities[i]).index = static_cast<int>(i);
  }
}

namespace {
std::optional<std::string> readText(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return std::nullopt;
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

void SceneSerializer::noteFileBefore(const std::string &resolvedPath) const {
  if (!m_journaling)
    return;
  for (const PrefabFileEdit &edit : m_journal)
    if (edit.path == resolvedPath)
      return;
  m_journal.push_back({resolvedPath, readText(resolvedPath), std::nullopt});
}

void SceneSerializer::beginFileJournal() {
  m_journaling = true;
  m_journal.clear();
}

std::vector<PrefabFileEdit> SceneSerializer::endFileJournal() {
  m_journaling = false;
  std::vector<PrefabFileEdit> edits = std::move(m_journal);
  m_journal.clear();
  for (PrefabFileEdit &edit : edits)
    edit.after = readText(edit.path);
  // A file written with the same content is no edit
  edits.erase(std::remove_if(edits.begin(), edits.end(), [](const PrefabFileEdit &edit) { return edit.before == edit.after; }),
              edits.end());
  return edits;
}

bool SceneSerializer::restorePrefabFile(const std::string &resolvedPath, const std::optional<std::string> &content) {
  const std::string key = fileKey(resolvedPath);
  std::error_code error;
  if (!content) {
    std::filesystem::remove(resolvedPath, error);
    m_prefabFiles.erase(key);
    return !error;
  }
  {
    std::ofstream out(resolvedPath, std::ios::binary);
    if (!out)
      return false;
    out << *content;
  }
  // The cache takes the file as it is now, with its new time, so the poll does not read it again and the instances
  // (already put back with the scene) are measured against the right baseline
  std::optional<json> scene = readFile(resolvedPath, false);
  if (!scene) {
    m_prefabFiles.erase(key);
    return true;
  }
  PrefabFile fresh;
  fresh.entities = std::move((*scene)["entities"]);
  fresh.time = std::filesystem::last_write_time(resolvedPath, error);
  fillPrefabFile(fresh);
  m_prefabFiles[key] = std::move(fresh);
  return true;
}

bool SceneSerializer::applyInstance(Entity root, const std::string &path) {
  const PrefabInstance *current = m_componentManager.tryGet<PrefabInstance>(root);
  if ((!current && path.empty()) || !m_entityManager.isAlive(root))
    return false;
  const std::string resolved = path.empty() ? EngineConfig::resolveAsset(current->path) : path;
  const std::string link = path.empty() ? current->path : EngineConfig::relativePath(path);

  std::vector<Entity> entities{root};
  const auto descendants = m_systemManager.getSystem<TransformSystem>().getDescendants(m_componentManager, root);
  entities.insert(entities.end(), descendants.begin(), descendants.end());
  std::vector<Entity> saved;
  json list = entitiesToJson(entities, root, &saved);
  noteFileBefore(resolved);
  if (!writeFile(resolved, std::move(list)))
    return false;

  // Every entity forgets its old place in a file, then the saved ones take their new one
  for (Entity entity : entities) {
    m_componentManager.removeComponent<PrefabPart>(entity);
    if (entity != root)
      m_componentManager.removeComponent<PrefabInstance>(entity);
  }
  const std::optional<json> written = readFile(resolved);
  if (!written || saved.empty() || saved.front() != root)
    return written.has_value();
  linkEntities(root, link, saved);

  // The file as written is what instances match from now on; the others follow it (this one already is it)
  PrefabFile fresh;
  fresh.entities = (*written)["entities"];
  std::error_code error;
  fresh.time = std::filesystem::last_write_time(resolved, error);
  fillPrefabFile(fresh);
  updateInstances(resolved, std::move(fresh), root);
  return true;
}

void SceneSerializer::revertInstance(Entity root) {
  const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(root);
  if (!instance)
    return;
  if (const PrefabFile *file = cachedPrefabFile(EngineConfig::resolveAsset(instance->path)))
    syncInstance(root, file->parts);
}

void SceneSerializer::unpackInstance(Entity root) {
  for (const auto &[index, entity] : collectParts(root))
    m_componentManager.removeComponent<PrefabPart>(entity);
  m_componentManager.removeComponent<PrefabInstance>(root);
}

size_t SceneSerializer::refreshPrefabs() {
  size_t updated = 0;
  std::vector<std::string> keys;
  keys.reserve(m_prefabFiles.size());
  for (const auto &entry : m_prefabFiles)
    keys.push_back(entry.first);
  for (const std::string &key : keys) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(key, error);
    PrefabFile &known = m_prefabFiles[key];
    known.exists = !error;
    if (error || time == known.time)
      continue;
    // Unreadable now (half written): tried again the next time, quietly
    std::optional<json> scene = readFile(key, false);
    if (!scene)
      continue;
    PrefabFile fresh;
    fresh.entities = std::move((*scene)["entities"]);
    fresh.time = time;
    fillPrefabFile(fresh);
    updated += updateInstances(key, std::move(fresh), -1);
  }
  return updated;
}

// The scene file's overrides, applied to the file as it is now. Without them (the file was missing at save) the
// entities stay as saved. The cache starts fresh for these files: a scene loads what is on disk now.
void SceneSerializer::syncLoadedInstances() {
  std::vector<std::pair<Entity, json>> pending = std::move(m_loadedOverrides);
  m_loadedOverrides.clear();
  std::unordered_set<std::string> reread;
  for (const auto &[root, overrides] : pending) {
    const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(root);
    if (!instance)
      continue;
    const std::string key = keyOf(*instance);
    std::error_code error;
    if (!std::filesystem::exists(key, error)) {
      if (reread.insert(key).second)
        LOG_W("[Scene] Prefab %s is missing; its instances keep what the scene saved", instance->path.c_str());
      continue;
    }
    if (reread.insert(key).second)
      m_prefabFiles.erase(key);
    const PrefabFile *file = prefabFile(key, false);
    if (!file) // not readable: the same
      continue;
    syncInstance(root, applyOverrides(file->parts, overrides));
  }
}
