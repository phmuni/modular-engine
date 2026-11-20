// Linked prefab instances: an instance remembers its prefab file, and follows it when the file changes while keeping
// what it changed itself; a prefab can hold instances of other prefabs. See the SceneSerializer header for the model.

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
#include <set>
#include <sstream>

namespace {
// Parts hold everything but what the instance owns: its place (Transform, the root's parent) and the link itself
bool isInstanceOwned(const std::string &key) { return key == "Transform" || key == "PrefabInstance" || key == "PrefabPart"; }

// The same file spelled two ways must give the same key
std::string fileKey(const std::string &resolvedPath) {
  return std::filesystem::path(resolvedPath).lexically_normal().generic_string();
}

std::string keyOf(const PrefabInstance &instance) { return fileKey(EngineConfig::resolveAsset(instance.path)); }

// A part's key: a number for an entity of the file itself, numbers joined by dots through nested prefabs ("2.0.1").
// A patch can add odd keys, which are ignored.
bool validKey(const std::string &key) {
  if (key.empty())
    return false;
  size_t digits = 0;
  for (char c : key) {
    if (c == '.') {
      if (digits == 0)
        return false;
      digits = 0;
    } else if (c >= '0' && c <= '9') {
      if (++digits > 6)
        return false;
    } else {
      return false;
    }
  }
  return digits > 0;
}

std::string lastSegment(const std::string &key) {
  const size_t dot = key.rfind('.');
  return dot == std::string::npos ? key : key.substr(dot + 1);
}

int segmentValue(const std::string &segment) { return static_cast<int>(std::strtol(segment.c_str(), nullptr, 10)); }

// `key` is `prefix` or below it
bool underKey(const std::string &key, const std::string &prefix) {
  return key == prefix || (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0 && key[prefix.size()] == '.');
}

// How a key is written in a part's "Parent": a plain number as files always did, a string for nested ones
nlohmann::json keyJson(const std::string &key) {
  return key.find('.') == std::string::npos ? nlohmann::json(segmentValue(key)) : nlohmann::json(key);
}

std::string keyString(const nlohmann::json &value, const std::string &fallback) {
  if (value.is_number_integer())
    return std::to_string(value.get<long long>());
  if (value.is_string())
    return value.get<std::string>();
  return fallback;
}

std::string parentKeyOf(const nlohmann::json &parent, const std::string &fallback) {
  return parent.is_object() && parent.contains("parent") ? keyString(parent["parent"], fallback) : fallback;
}

const nlohmann::json &componentsOf(const nlohmann::json &part) {
  static const nlohmann::json none = nlohmann::json::object();
  return part.is_object() && part.contains("components") && part["components"].is_object() ? part["components"] : none;
}

// The nested instance an entry of a prefab file holds, if it does: the "PrefabInstance" it carries
const nlohmann::json *nestedLinkOf(const nlohmann::json &entry) {
  const nlohmann::json &components = componentsOf(entry);
  if (!components.contains("PrefabInstance"))
    return nullptr;
  const nlohmann::json &link = components["PrefabInstance"];
  return link.is_object() && link.contains("path") && link["path"].is_string() ? &link : nullptr;
}

bool contains(const std::vector<std::string> &list, const std::string &value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

void addUnique(std::vector<std::string> &list, const std::string &value) {
  if (!contains(list, value))
    list.push_back(value);
}
} // namespace

void SceneSerializer::fillPrefabFile(PrefabFile &file, const std::string &key) const {
  file.parts = json::object();
  file.root = -1;
  file.opaque.clear();
  file.deps.clear();
  file.missing.clear();
  file.cyclic = false;
  m_resolving.push_back(key);
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
    if (const json *link = nestedLinkOf(entry))
      expandNested(file, i, part, *link);
    else
      file.parts[std::to_string(i)] = std::move(part);
  }
  m_resolving.pop_back();
  roundFloats(file.parts);
  if (roots != 1)
    file.root = -1;
}

// The entry `index` holds another prefab: its parts, with the overrides this file makes to them, go in under the
// keys "index.<their key>"; the nested root is the entry itself ("index"), with this file's Parent
void SceneSerializer::expandNested(PrefabFile &file, size_t index, json &part, const json &link) const {
  const std::string at = std::to_string(index);
  const std::string path = link["path"].get<std::string>();
  const std::string innerKey = fileKey(EngineConfig::resolveAsset(path));
  const std::string &outerKey = m_resolving.back();
  const json ownParent = componentsOf(part).contains("Parent") ? componentsOf(part)["Parent"] : json();
  const json marker = {{"path", path}};
  addUnique(file.deps, innerKey);

  // What is left of the entry when the nested prefab cannot be expanded: where it sits and that it is one
  auto unexpanded = [&]() {
    json components = json::object();
    if (!ownParent.is_null())
      components["Parent"] = ownParent;
    components["PrefabInstance"] = marker;
    file.parts[at] = {{"components", std::move(components)}};
    addUnique(file.opaque, at);
  };

  if (contains(m_resolving, innerKey)) {
    LOG_E("[Scene] Prefab %s contains itself (through %s); the nested instance is left empty", outerKey.c_str(),
          innerKey.c_str());
    file.cyclic = true;
    unexpanded();
    return;
  }
  const PrefabFile *inner = cachedPrefabFile(innerKey);
  if (!inner) {
    std::error_code error;
    if (!std::filesystem::exists(innerKey, error)) {
      addUnique(file.missing, innerKey);
      LOG_W("[Scene] Prefab %s nested in %s is missing; its entities stay as they are", innerKey.c_str(), outerKey.c_str());
    } else {
      LOG_W("[Scene] Prefab %s nested in %s cannot be read; its entities stay as they are", innerKey.c_str(),
            outerKey.c_str());
    }
    unexpanded();
    return;
  }
  for (const std::string &dep : inner->deps)
    addUnique(file.deps, dep);
  for (const std::string &gone : inner->missing)
    addUnique(file.missing, gone);
  if (inner->cyclic) {
    LOG_E("[Scene] Prefab %s nested in %s contains itself; the nested instance is left empty", innerKey.c_str(),
          outerKey.c_str());
    file.cyclic = true;
    unexpanded();
    return;
  }
  if (inner->root < 0) {
    LOG_W("[Scene] Prefab %s nested in %s has no single root; its entities stay as they are", innerKey.c_str(),
          outerKey.c_str());
    unexpanded();
    return;
  }

  const json patched = applyOverrides(inner->parts, link.contains("overrides") ? link["overrides"] : json::array());
  const std::string rootKey = std::to_string(inner->root);
  auto outer = [&](const std::string &key) { return key == rootKey ? at : at + "." + key; };
  for (const auto &item : patched.items()) {
    if (!validKey(item.key()) || !item.value().is_object())
      continue;
    json expanded = item.value();
    json components = componentsOf(expanded);
    if (components.contains("Parent") && components["Parent"].is_object())
      components["Parent"]["parent"] = keyJson(outer(parentKeyOf(components["Parent"], rootKey)));
    if (item.key() == rootKey) {
      components.erase("Parent");
      if (!ownParent.is_null())
        components["Parent"] = ownParent;
      components["PrefabInstance"] = marker;
    }
    expanded["components"] = std::move(components);
    file.parts[outer(item.key())] = std::move(expanded);
  }
  for (const std::string &key : inner->opaque)
    addUnique(file.opaque, outer(key));
  if (!file.parts.contains(at)) // an override removed the nested root: it is not expanded
    unexpanded();
}

const SceneSerializer::PrefabFile *SceneSerializer::cachedPrefabFile(const std::string &resolvedPath) const {
  const std::string key = fileKey(resolvedPath);
  auto it = m_prefabFiles.find(key);
  if (it != m_prefabFiles.end()) {
    // A file nested in it changed: its parts are made again now
    if (m_stale.erase(key))
      fillPrefabFile(it->second, key);
    return &it->second;
  }
  // Not read yet (a scene saved before anything used it): what the instances have becomes the baseline
  std::optional<json> scene = readFile(key, false);
  if (!scene)
    return nullptr;
  std::error_code error;
  PrefabFile file;
  file.entities = std::move((*scene)["entities"]);
  file.time = std::filesystem::last_write_time(key, error);
  fillPrefabFile(file, key);
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
    fillPrefabFile(file, key);
    const std::vector<std::string> deps = file.deps;
    m_prefabFiles.emplace(key, std::move(file));
    if (checkChanged && !deps.empty())
      refreshKeys(deps, true); // a nested file edited since it was read (the files that hold it follow)
    return &m_prefabFiles.find(key)->second;
  }
  if (!checkChanged)
    return cachedPrefabFile(key);
  // A spawn notices a file edited since it was read, so a hand-edited prefab is used without restarting
  std::vector<std::string> keys{key};
  keys.insert(keys.end(), it->second.deps.begin(), it->second.deps.end());
  refreshKeys(keys, true);
  return cachedPrefabFile(key);
}

SceneSerializer::PartList SceneSerializer::collectParts(Entity root) const {
  PartList parts;
  const PrefabPart *rootPart = m_componentManager.tryGet<PrefabPart>(root);
  parts.emplace_back(std::to_string(rootPart ? rootPart->index : 0), root);
  std::unordered_set<std::string> seen{parts.front().first};
  auto &transformSystem = m_systemManager.getSystem<TransformSystem>();
  // Each entity with the prefix its children's keys get: nothing in the instance's own file, "<key>." below a nested
  // instance (whose parts are keyed through it)
  std::vector<std::pair<Entity, std::string>> pending{{root, std::string()}};
  while (!pending.empty() && parts.size() < 100000) {
    const auto [entity, prefix] = pending.back();
    pending.pop_back();
    for (Entity child : transformSystem.getChildren(m_componentManager, entity)) {
      const PrefabPart *part = m_componentManager.tryGet<PrefabPart>(child);
      if (m_componentManager.has<PrefabInstance>(child)) {
        if (!part || part->outer < 0)
          continue; // another instance, parented here: not part of this one
        const std::string key = prefix + std::to_string(part->outer);
        if (seen.insert(key).second)
          parts.emplace_back(key, child);
        pending.emplace_back(child, key + ".");
        continue;
      }
      if (part) {
        const std::string key = prefix + std::to_string(part->index);
        if (seen.insert(key).second)
          parts.emplace_back(key, child);
      }
      pending.emplace_back(child, prefix); // entities added to the instance can hold parts too
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

Entity SceneSerializer::enclosingInstance(Entity root) const {
  const PrefabPart *part = m_componentManager.tryGet<PrefabPart>(root);
  if (!part || part->outer < 0 || !m_componentManager.has<PrefabInstance>(root))
    return -1;
  const Parent *parent = m_componentManager.tryGet<Parent>(root);
  Entity at = parent ? parent->entity : -1;
  for (int guard = 0; guard < 256 && at != -1; ++guard) {
    if (const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(at))
      return cachedPrefabFile(EngineConfig::resolveAsset(instance->path)) ? at : -1;
    const Parent *up = m_componentManager.tryGet<Parent>(at);
    at = up ? up->entity : -1;
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

std::vector<Entity> SceneSerializer::topInstancesOf(const std::string &key) const {
  std::vector<Entity> instances = instancesOf(key);
  instances.erase(std::remove_if(instances.begin(), instances.end(),
                                 [&](Entity entity) { return enclosingInstance(entity) != -1; }),
                  instances.end());
  return instances;
}

bool SceneSerializer::prefabFileExists(Entity root) const {
  const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(root);
  std::error_code error;
  return instance && std::filesystem::exists(EngineConfig::resolveAsset(instance->path), error);
}

// One part as the prefab file would hold it (see fillPrefabFile): without what the instance owns
SceneSerializer::json SceneSerializer::livePart(Entity entity, Entity root, const std::map<Entity, std::string> &keyOf) const {
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
    auto it = keyOf.find(parent->entity);
    components["Parent"] = {{"parent", keyJson(it != keyOf.end() ? it->second : keyOf.at(root))}, {"local", parent->local}};
  }
  // A nested instance's root says which prefab it is
  if (entity != root)
    if (const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(entity))
      components["PrefabInstance"] = {{"path", instance->path}};
  part["components"] = std::move(components);
  return part;
}

SceneSerializer::json SceneSerializer::liveParts(Entity root, const PartList &parts,
                                                 const std::vector<std::string> &opaque) const {
  std::map<Entity, std::string> keys;
  for (const auto &[key, entity] : parts)
    keys[entity] = key;
  json result = json::object();
  for (const auto &[key, entity] : parts) {
    bool below = false, atRoot = false;
    for (const std::string &o : opaque) {
      atRoot = atRoot || key == o;
      below = below || (key != o && underKey(key, o));
    }
    if (below)
      continue; // not known: nothing to compare
    json part = livePart(entity, root, keys);
    if (atRoot) { // only its place and that it is one
      json components = json::object();
      for (const char *kept : {"Parent", "PrefabInstance"})
        if (componentsOf(part).contains(kept))
          components[kept] = componentsOf(part)[kept];
      part = {{"components", std::move(components)}};
    }
    result[key] = std::move(part);
  }
  roundFloats(result);
  return result;
}

// What an instance is compared with: its file's parts. A nested instance is compared with its outer prefab's view of
// it, so that it shows (and reverts) only what is changed in the scene
SceneSerializer::Baseline SceneSerializer::baselineOf(Entity root) const {
  Baseline baseline;
  const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(root);
  if (!instance)
    return baseline;
  const Entity outer = enclosingInstance(root);
  if (outer == -1) {
    const PrefabFile *file = cachedPrefabFile(EngineConfig::resolveAsset(instance->path));
    if (!file)
      return baseline;
    baseline.parts = file->parts;
    baseline.opaque = file->opaque;
    baseline.ok = true;
    return baseline;
  }
  const Baseline outerBaseline = baselineOf(outer);
  const PrefabPart *part = m_componentManager.tryGet<PrefabPart>(root);
  if (!outerBaseline.ok || !part)
    return baseline;
  // The part of it below the nested root, keyed as the nested prefab's own file keys them
  const std::string at = std::to_string(part->outer), rootKey = std::to_string(part->index);
  auto inner = [&](const std::string &key) { return key == at ? rootKey : key.substr(at.size() + 1); };
  baseline.parts = json::object();
  for (const auto &item : outerBaseline.parts.items()) {
    if (!underKey(item.key(), at) || !item.value().is_object())
      continue;
    json copy = item.value();
    json components = componentsOf(copy);
    if (item.key() == at) {
      components.erase("Parent");
      components.erase("PrefabInstance");
    } else if (components.contains("Parent") && components["Parent"].is_object()) {
      const std::string parentKey = parentKeyOf(components["Parent"], at);
      components["Parent"]["parent"] = keyJson(underKey(parentKey, at) ? inner(parentKey) : rootKey);
    }
    copy["components"] = std::move(components);
    baseline.parts[inner(item.key())] = std::move(copy);
  }
  for (const std::string &o : outerBaseline.opaque)
    if (underKey(o, at))
      baseline.opaque.push_back(inner(o));
  baseline.ok = true;
  return baseline;
}

// The changes an instance made, as a JSON Patch from the file's parts to its own
SceneSerializer::json SceneSerializer::instanceOverridesPatch(Entity root) const {
  const Baseline baseline = baselineOf(root);
  if (!baseline.ok)
    return nullptr;
  return json::diff(baseline.parts, liveParts(root, collectParts(root), baseline.opaque));
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
  const Baseline baseline = baselineOf(root);
  std::vector<std::string> paths;
  if (!baseline.ok)
    return paths;
  const json patch = json::diff(baseline.parts, liveParts(root, collectParts(root), baseline.opaque));
  if (!patch.is_array())
    return paths;
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
    if (baseline.parts.contains(tokens[0]) && baseline.parts[tokens[0]].contains("name"))
      label = baseline.parts[tokens[0]]["name"].get<std::string>();
    if (tokens.size() == 1)
      label += operation.value("op", "") == "remove" ? " (removed)" : " (added)";
    for (size_t i = (tokens.size() > 1 && tokens[1] == "components") ? 2 : 1; i < tokens.size(); ++i)
      label += "/" + tokens[i];
    if (std::find(paths.begin(), paths.end(), label) == paths.end())
      paths.push_back(std::move(label));
  }
  return paths;
}

void SceneSerializer::syncInstance(Entity root, const json &target, const std::vector<std::string> &opaque, bool refresh) {
  auto &sceneSystem = m_systemManager.getSystem<SceneSystem>();
  auto &transformSystem = m_systemManager.getSystem<TransformSystem>();
  const PartList parts = collectParts(root);
  std::map<std::string, Entity> entityOf;
  for (const auto &[key, entity] : parts)
    entityOf[key] = entity;
  const json current = liveParts(root, parts, opaque);
  const std::string rootKey = parts.front().first;
  auto isOpaque = [&](const std::string &key) {
    return std::any_of(opaque.begin(), opaque.end(), [&](const std::string &o) { return underKey(key, o); });
  };
  bool changed = false;

  // Parts the file no longer has go; what was attached to them stays, on the root
  std::vector<Entity> gone;
  for (const auto &[key, entity] : parts)
    if (entity != root && !target.contains(key) && !isOpaque(key))
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
    it = (it->second != root && !target.contains(it->first) && !isOpaque(it->first)) ? entityOf.erase(it) : std::next(it);

  // Parts it has that the instance does not (new in the file, or removed by the instance and not by an override)
  std::vector<std::string> created;
  for (const auto &item : target.items()) {
    const std::string &key = item.key();
    if (!validKey(key) || entityOf.count(key))
      continue;
    const Entity entity = m_entityManager.createEntity();
    entityOf[key] = entity;
    created.push_back(key);
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
    PrefabPart &place = m_componentManager.addInPlace<PrefabPart>(entity);
    const int last = segmentValue(lastSegment(key));
    if (const json *link = nestedLinkOf(item.value())) {
      // The root of a nested instance: linked to its file, a part of this one at `last`
      const std::string path = (*link)["path"].get<std::string>();
      m_componentManager.addInPlace<PrefabInstance>(entity).path = path;
      const PrefabFile *inner = cachedPrefabFile(EngineConfig::resolveAsset(path));
      place.index = inner && inner->root >= 0 ? inner->root : 0;
      place.outer = last;
    } else {
      place.index = last;
      place.outer = -1;
    }
    changed = true;
  }

  // Parts both have: only what differs is touched, so nothing else (a body's state, a model's materials) is reset
  for (const auto &[key, entity] : entityOf) {
    if (std::find(created.begin(), created.end(), key) != created.end() || isOpaque(key))
      continue;
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
    std::string key;
  };
  std::vector<Place> places;
  for (const auto &item : target.items()) {
    const std::string &key = item.key();
    if (!validKey(key) || key == rootKey || !entityOf.count(key) || !componentsOf(item.value()).contains("Parent"))
      continue;
    int depth = 0;
    std::string at = key;
    while (at != rootKey && depth < 64 && target.contains(at) && componentsOf(target[at]).contains("Parent")) {
      at = parentKeyOf(componentsOf(target[at])["Parent"], rootKey);
      ++depth;
    }
    places.push_back({depth, key});
  }
  std::stable_sort(places.begin(), places.end(), [](const Place &a, const Place &b) { return a.depth < b.depth; });
  for (const Place &place : places) {
    const Entity entity = entityOf[place.key];
    const json &wantParent = componentsOf(target[place.key])["Parent"];
    const std::string parentKey = parentKeyOf(wantParent, rootKey);
    const Entity parent = entityOf.count(parentKey) ? entityOf[parentKey] : root;
    const bool isNew = std::find(created.begin(), created.end(), place.key) != created.end();
    const bool sameLocal = !isNew && current.contains(place.key) && componentsOf(current[place.key]).contains("Parent") &&
                           componentsOf(current[place.key])["Parent"] == wantParent;
    if (sameLocal)
      continue;
    if (transformSystem.getParent(m_componentManager, entity) != parent)
      transformSystem.setParent(m_componentManager, entity, parent);
    transformSystem.setLocalTransform(m_componentManager, entity, wantParent.value("local", Transform()));
    changed = true;
  }

  if (!changed)
    return;
  if (refresh) {
    // What undo and Stop do when they replace components: everything that read them starts again from them
    m_systemManager.getSystem<LightSystem>().rebuild(m_componentManager);
    m_systemManager.getSystem<RenderSystem>().markBatchesDirty();
    m_systemManager.getSystem<PhysicsSystem>().reset();
    m_systemManager.getSystem<NavigationSystem>().markDirty();
  }
  transformSystem.updateHierarchy(m_componentManager);
}

size_t SceneSerializer::updateInstances(std::map<std::string, PrefabFile> fresh, Entity skip) {
  // The files that changed and the ones that hold them (nested, however deep)
  std::set<std::string> affected;
  for (const auto &[key, file] : fresh)
    affected.insert(key);
  for (const auto &[key, file] : m_prefabFiles)
    for (const auto &[changedKey, changedFile] : fresh)
      if (contains(file.deps, changedKey))
        affected.insert(key);

  struct Pending {
    Entity instance;
    std::string key;
    json overrides;
  };
  std::vector<Pending> pending;
  for (const std::string &key : affected) {
    auto old = m_prefabFiles.find(key);
    for (Entity instance : topInstancesOf(key)) {
      if (instance == skip)
        continue;
      // What it changed from the file as it was; measured before the file's new content replaces it
      json overrides = old != m_prefabFiles.end()
                           ? json::diff(old->second.parts, liveParts(instance, collectParts(instance), old->second.opaque))
                           : json::array();
      pending.push_back({instance, key, std::move(overrides)});
    }
  }
  for (auto &[key, file] : fresh) {
    PrefabFile &stored = m_prefabFiles[key];
    stored.entities = std::move(file.entities);
    stored.time = file.time;
    stored.exists = true;
  }
  for (const std::string &key : affected)
    m_stale.insert(key);
  for (const std::string &key : affected)
    cachedPrefabFile(key); // fills the stale ones, the files nested in them first
  for (const Pending &item : pending) {
    const PrefabFile *file = cachedPrefabFile(item.key);
    if (file)
      syncInstance(item.instance, applyOverrides(file->parts, item.overrides), file->opaque);
  }
  if (!pending.empty())
    LOG_I("[Scene] %zu prefab file(s) changed: %zu instance(s) updated", fresh.size(), pending.size());
  return pending.size();
}

void SceneSerializer::refillDependents(const std::string &key) {
  std::vector<std::string> dependents;
  for (const auto &[other, file] : m_prefabFiles)
    if (contains(file.deps, key))
      dependents.push_back(other);
  for (const std::string &other : dependents)
    m_stale.insert(other);
  for (const std::string &other : dependents)
    cachedPrefabFile(other);
}

size_t SceneSerializer::refreshKeys(const std::vector<std::string> &keys, bool log) {
  std::map<std::string, PrefabFile> fresh;
  for (const std::string &key : keys) {
    auto it = m_prefabFiles.find(key);
    if (it == m_prefabFiles.end() || fresh.count(key))
      continue;
    PrefabFile &known = it->second;
    std::error_code error;
    const auto time = std::filesystem::last_write_time(key, error);
    known.exists = !error;
    if (!error && time != known.time) {
      // Unreadable now (half written): tried again the next time
      if (std::optional<json> scene = readFile(key, log)) {
        PrefabFile changed;
        changed.entities = std::move((*scene)["entities"]);
        changed.time = time;
        fresh.emplace(key, std::move(changed));
      }
      continue;
    }
    // A file nested in it that was not there appeared
    for (const std::string &missing : known.missing) {
      std::error_code exists;
      if (std::filesystem::exists(missing, exists)) {
        PrefabFile again;
        again.entities = known.entities;
        again.time = known.time;
        fresh.emplace(key, std::move(again));
        break;
      }
    }
  }
  if (fresh.empty())
    return 0;
  return updateInstances(std::move(fresh), -1);
}

void SceneSerializer::linkEntities(Entity root, const std::string &path, const std::vector<Entity> &entities,
                                   const std::unordered_set<Entity> &references) {
  for (size_t i = 0; i < entities.size(); ++i) {
    const Entity entity = entities[i];
    if (entity == root) {
      m_componentManager.addInPlace<PrefabInstance>(root).path = path;
      PrefabPart &part = m_componentManager.addInPlace<PrefabPart>(entity);
      part.index = static_cast<int>(i);
      part.outer = -1;
    } else if (references.count(entity)) {
      // A nested instance: its place in its own file stays, and it is entry i of this one
      const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(entity);
      const PrefabPart *old = m_componentManager.tryGet<PrefabPart>(entity);
      int index = old ? old->index : 0;
      if (!old && instance)
        if (const PrefabFile *inner = cachedPrefabFile(EngineConfig::resolveAsset(instance->path)))
          index = std::max(inner->root, 0);
      PrefabPart &part = m_componentManager.addInPlace<PrefabPart>(entity);
      part.index = index;
      part.outer = static_cast<int>(i);
    } else {
      m_componentManager.removeComponent<PrefabInstance>(entity); // an instance inside that is not kept is flattened
      PrefabPart &part = m_componentManager.addInPlace<PrefabPart>(entity);
      part.index = static_cast<int>(i);
      part.outer = -1;
    }
  }
}

bool SceneSerializer::canReference(Entity entity) const {
  const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(entity);
  if (!instance)
    return false;
  const PrefabFile *file = cachedPrefabFile(EngineConfig::resolveAsset(instance->path));
  return file && file->root >= 0 && !file->cyclic;
}

std::optional<SceneSerializer::json> SceneSerializer::nestedLink(Entity entity) const {
  const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(entity);
  if (!instance)
    return std::nullopt;
  const PrefabFile *file = cachedPrefabFile(EngineConfig::resolveAsset(instance->path));
  if (!file || file->root < 0 || file->cyclic)
    return std::nullopt;
  // Everything that differs from the nested prefab's file, whether the scene or this prefab made the change
  json link = {{"path", instance->path}};
  const json patch = json::diff(file->parts, liveParts(entity, collectParts(entity), file->opaque));
  if (patch.is_array() && !patch.empty())
    link["overrides"] = patch;
  return link;
}

std::vector<Entity> SceneSerializer::prefabEntities(Entity root) const {
  std::vector<Entity> list{root};
  auto &transformSystem = m_systemManager.getSystem<TransformSystem>();
  for (size_t i = 0; i < list.size() && list.size() < 100000; ++i) {
    const Entity entity = list[i];
    if (entity != root && canReference(entity)) {
      // One entry stands for the whole nested instance; what was added inside it is not part of its prefab
      std::unordered_set<Entity> held;
      for (const auto &[key, part] : collectParts(entity))
        held.insert(part);
      size_t extra = 0;
      for (Entity inside : transformSystem.getDescendants(m_componentManager, entity))
        extra += held.count(inside) ? 0 : 1;
      if (extra > 0)
        LOG_W("[Scene] %zu entit%s added inside the nested prefab instance %d (%s) %s not part of its prefab: the prefab being "
              "written does not keep %s",
              extra, extra == 1 ? "y" : "ies", entity, m_componentManager.tryGet<PrefabInstance>(entity)->path.c_str(),
              extra == 1 ? "is" : "are", extra == 1 ? "it" : "them");
      continue;
    }
    for (Entity child : transformSystem.getChildren(m_componentManager, entity))
      list.push_back(child);
  }
  return list;
}

std::string SceneSerializer::nestingCycle(const std::string &targetKey, Entity root, const std::vector<Entity> &entities) const {
  for (Entity entity : entities) {
    const PrefabInstance *instance = entity != root ? m_componentManager.tryGet<PrefabInstance>(entity) : nullptr;
    if (!instance)
      continue;
    const std::string inner = keyOf(*instance);
    if (inner == targetKey)
      return inner;
    if (const PrefabFile *file = cachedPrefabFile(inner))
      if (contains(file->deps, targetKey))
        return inner;
  }
  return {};
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
    refillDependents(key);
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
    refillDependents(key);
    return true;
  }
  PrefabFile fresh;
  fresh.entities = std::move((*scene)["entities"]);
  fresh.time = std::filesystem::last_write_time(resolvedPath, error);
  fillPrefabFile(fresh, key);
  m_prefabFiles[key] = std::move(fresh);
  refillDependents(key); // the prefabs that hold this one see it as it is now
  return true;
}

bool SceneSerializer::applyInstance(Entity root, const std::string &path) {
  const PrefabInstance *current = m_componentManager.tryGet<PrefabInstance>(root);
  if ((!current && path.empty()) || !m_entityManager.isAlive(root))
    return false;
  // A nested instance written to another file is a copy: it stays what it is in the prefab that holds it
  if (!path.empty() && enclosingInstance(root) != -1)
    return savePrefab(root, path);
  const std::string resolved = path.empty() ? EngineConfig::resolveAsset(current->path) : path;
  const std::string link = path.empty() ? current->path : EngineConfig::relativePath(path);
  const int outerIndex = path.empty() && m_componentManager.has<PrefabPart>(root) ? m_componentManager.tryGet<PrefabPart>(root)->outer : -1;

  const std::vector<Entity> entities = prefabEntities(root);
  const std::string cycle = nestingCycle(fileKey(EngineConfig::resolveAsset(resolved)), root, entities);
  if (!cycle.empty()) {
    LOG_E("[Scene] Not writing %s: it would contain itself (through %s)", resolved.c_str(), cycle.c_str());
    return false;
  }
  std::unordered_set<Entity> references;
  for (Entity entity : entities)
    if (entity != root && canReference(entity))
      references.insert(entity);

  std::vector<Entity> saved;
  json list = entitiesToJson(entities, root, &saved);
  noteFileBefore(resolved);
  if (!writeFile(resolved, std::move(list)))
    return false;

  // Every entity forgets its old place in a file (a nested instance keeps the one in its own), then the saved ones
  // take their new one
  for (Entity entity : entities) {
    if (references.count(entity))
      continue;
    m_componentManager.removeComponent<PrefabPart>(entity);
    if (entity != root)
      m_componentManager.removeComponent<PrefabInstance>(entity);
  }
  const std::optional<json> written = readFile(resolved);
  if (!written || saved.empty() || saved.front() != root)
    return written.has_value();
  linkEntities(root, link, saved, references);
  if (outerIndex >= 0)
    m_componentManager.addInPlace<PrefabPart>(root).outer = outerIndex; // it is still a part of the prefab holding it

  // The file as written is what instances match from now on; the others follow it (this one already is it)
  PrefabFile fresh;
  fresh.entities = (*written)["entities"];
  std::error_code error;
  fresh.time = std::filesystem::last_write_time(resolved, error);
  std::map<std::string, PrefabFile> changed;
  changed.emplace(fileKey(resolved), std::move(fresh));
  updateInstances(std::move(changed), root);
  return true;
}

void SceneSerializer::revertInstance(Entity root) {
  const Baseline baseline = baselineOf(root);
  if (baseline.ok)
    syncInstance(root, baseline.parts, baseline.opaque);
}

void SceneSerializer::unpackInstance(Entity root) {
  // This level's entities forget the file; the parts of instances nested inside belong to those and stay as they are,
  // and the nested roots stand alone from here on
  for (const auto &[key, entity] : collectParts(root)) {
    if (key.find('.') != std::string::npos)
      continue;
    if (entity != root && m_componentManager.has<PrefabInstance>(entity)) {
      if (PrefabPart *part = m_componentManager.tryGet<PrefabPart>(entity))
        part->outer = -1;
    } else {
      m_componentManager.removeComponent<PrefabPart>(entity);
    }
  }
  m_componentManager.removeComponent<PrefabPart>(root);
  m_componentManager.removeComponent<PrefabInstance>(root);
}

size_t SceneSerializer::refreshPrefabs() {
  std::vector<std::string> keys;
  keys.reserve(m_prefabFiles.size());
  for (const auto &entry : m_prefabFiles)
    keys.push_back(entry.first);
  return refreshKeys(keys, false);
}

// The scene file's overrides, applied to the file as it is now. Without them (the file was missing at save) the
// entities stay as saved. The cache starts fresh for these files: a scene loads what is on disk now.
void SceneSerializer::syncLoadedInstances() {
  std::vector<std::pair<Entity, json>> pending = std::move(m_loadedOverrides);
  m_loadedOverrides.clear();
  if (pending.empty())
    return;
  m_prefabFiles.clear();
  m_stale.clear();
  std::unordered_set<std::string> reported;
  for (const auto &[root, overrides] : pending) {
    const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(root);
    if (!instance)
      continue;
    const std::string key = keyOf(*instance);
    std::error_code error;
    if (!std::filesystem::exists(key, error)) {
      if (reported.insert(key).second)
        LOG_W("[Scene] Prefab %s is missing; its instances keep what the scene saved", instance->path.c_str());
      continue;
    }
    const PrefabFile *file = prefabFile(key, false);
    if (!file) // not readable: the same
      continue;
    if (enclosingInstance(root) != -1) // the prefab that holds it brings it up to date
      continue;
    syncInstance(root, applyOverrides(file->parts, overrides), file->opaque);
  }
}
