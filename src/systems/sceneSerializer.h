#pragma once
// Saves the entities of the running scene to a JSON file and loads them back, replacing what is there.
//
// Each component is written under a key ("Transform", "Model"...) by a registered serializer; the engine registers
// its own, and games register theirs (registerComponent<T>) to save their components too. Components without a
// serializer are left out of the file, and keys in a file that nothing registered are skipped with a warning.

#include "foundation/core/collisionLayers.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include <filesystem>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct ReflectedComponent;

class SceneSerializer : public BaseSystem {
public:
  using json = nlohmann::json;
  // Returns nothing when the entity does not have the component
  using SaveFn = std::function<std::optional<json>(Entity)>;
  using LoadFn = std::function<void(Entity, const json &)>;
  // Takes the component off an entity; only needed to update linked prefab instances (a component the prefab no
  // longer has, or one that is loaded again from changed data)
  using RemoveFn = std::function<void(Entity)>;

  SceneSerializer(EntityManager &em, ComponentManager &cm, SystemManager &sm);

  // Components load in registration order, so one can rely on those registered before it (Transform comes first)
  void registerComponent(const std::string &key, SaveFn save, LoadFn load, RemoveFn remove = {});

  // For plain data with to_json/from_json (e.g. NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT)
  template <typename T> void registerComponent(const std::string &key) {
    registerComponent(
        key,
        [this](Entity entity) -> std::optional<json> {
          if (const T *component = m_componentManager.tryGet<T>(entity))
            return json(*component);
          return std::nullopt;
        },
        [this](Entity entity, const json &data) { m_componentManager.addInPlace<T>(entity) = data.get<T>(); },
        [this](Entity entity) { m_componentManager.removeComponent<T>(entity); });
  }

  // A component described by its fields (Engine::registerComponent): saved as a JSON object of them under its name.
  // Fields a file does not have keep the component's defaults. `component` must outlive the serializer.
  void registerReflected(const ReflectedComponent &component);

  // The layers a scene file carries (see load). Without them, scenes neither save nor change the layers.
  void setCollisionLayers(CollisionLayers &layers) { m_layers = &layers; }

  // Scene files also hold the collision layers when they are not the default (a "layers" key in the format of
  // project.json: the layers beyond the built-in ones, and the pairs that do not collide), saved as they are when
  // saving: the project's, the game's code's and the scene's together. On load the file's layers are merged into the
  // base the project.json or the game's setup defined: a scene can add layers and switch pairs off, never remove a
  // layer or switch a pair the base turned off back on. The next load starts again from that base (when nothing
  // changed the layers in between; changes made by code after a load become the new base), so layers do not pile up
  // from scene to scene. A file without the key leaves the base alone, as old scenes did.
  bool save(const std::string &path) const;
  // Reads the whole file first; if it cannot be read, the current scene stays
  bool load(const std::string &path);

  // The scene as compact JSON text (what save() writes, entity numbers aside), to tell whether two moments differ
  std::string fingerprint() const;

  // Prefabs: an entity and its descendants in a file of the same format, the root at the origin
  bool savePrefab(Entity root, const std::string &path) const;
  // Adds a file's entities to the scene, keeping what is there; returns them in file order (empty if unreadable).
  // A prefab with one root comes linked to its file (see below).
  std::vector<Entity> instantiate(const std::string &path);

  // Linked prefab instances. A copy made by instantiate() remembers its file (PrefabInstance on the root, PrefabPart
  // on each entity from the file), so when the file changes the instances change with it. What an instance changed
  // from the file (its overrides: any field of any component, entities it removed) is kept through the update; the
  // root's own Transform and parent always belong to the instance. Overrides are never stored: they are the JSON
  // diff between the instance and the file as it was when the instance last matched it (kept in memory, one copy
  // per prefab in use), found when saving a scene, on Apply, and on update.
  // Scene files hold every entity in full as before, plus the link on the root ("PrefabInstance": path and
  // overrides), so a missing prefab file leaves the content as saved, and older scenes load as they are. When the
  // file exists at load, the instance is brought up to date with it.
  // Not covered: a prefab inside a prefab (a prefab file never holds links, so one saved from an instance with
  // another instance inside holds that one flattened), and files with several roots (not linked).

  // The root of the linked instance `entity` belongs to (itself, if it is one), or -1
  Entity instanceRoot(Entity entity) const;
  // The linked instance roots whose file is `path` (resolved)
  std::vector<Entity> instancesOf(const std::string &resolvedPath) const;
  // Whether the instance's file exists on disk
  bool prefabFileExists(Entity root) const;
  // The instance's overrides as the paths they change ("Wheel/Model/opacity"), empty when it matches its file
  std::vector<std::string> instanceOverrides(Entity root);
  // Writes the instance (the root with all its descendants) to its file, so every instance follows it, and links
  // the entities to it. `path` is another file to write instead (Save as prefab): the instance is then linked to it.
  bool applyInstance(Entity root, const std::string &path = {});
  // Drops the overrides: the instance matches the file again (the root keeps its place)
  void revertInstance(Entity root);
  // Breaks the link: plain entities from now on
  void unpackInstance(Entity root);
  // Rereads the prefab files in use that changed on disk and updates their instances; returns how many it updated.
  // Only a stat per file, but it is not free: the editor calls it a few times a second while it is open.
  size_t refreshPrefabs();
  // The entity and its descendants as scene-file text (what the editor copies to the clipboard), the root keeping its
  // place in the world but not its parent; empty if the entity does not exist
  std::string copyToJson(Entity root) const;
  // Several roots at once, each with its descendants
  std::string copyToJson(const std::vector<Entity> &roots) const;
  // Adds the entities of such text to the scene; returns them in order, the root first (empty if the text is not
  // a scene or prefab in this format)
  std::vector<Entity> pasteFromJson(const std::string &text);
  // Destroys every entity
  void clear();

  // The model files a scene file uses (resolved paths), to read them ahead of loading it; empty if it cannot be read
  std::vector<std::string> modelPaths(const std::string &path) const;

  // The .json files in a folder, sorted, as paths relative to the executable's folder
  std::vector<std::string> listScenes(const std::string &directory) const;

private:
  struct Serializer {
    std::string key;
    SaveFn save;
    LoadFn load;
    RemoveFn remove;
  };

  EntityManager &m_entityManager;
  ComponentManager &m_componentManager;
  CollisionLayers *m_layers = nullptr;
  nlohmann::json m_baseLayers;      // the layers before the last scene merged its own (CollisionLayers::toJson)
  bool m_sceneLayersMerged = false; // the last load merged some, so they are in effect until the layers change
  uint32_t m_sceneLayersVersion = 0;
  void mergeSceneLayers(const json &scene);
  SystemManager &m_systemManager;
  std::vector<Serializer> m_serializers;
  mutable std::unordered_set<std::string> m_warnedKeys;

  // Linked prefab instances (sceneSerializerPrefabs.cpp)
  struct PrefabFile {
    json entities;                          // the file's "entities" list, as read
    json parts;                             // by index ("0", "1"...): what an instance is compared with
    int root = -1;                          // the one entity without a parent; -1 if there is not exactly one
    std::filesystem::file_time_type time{}; // when it was read
    bool exists = true;
  };
  // By resolved path. Mutable: saving a scene (const) may find a file not read yet.
  mutable std::unordered_map<std::string, PrefabFile> m_prefabFiles;
  // Set while save() writes a scene: PrefabInstance then holds the overrides too (not wanted in undo copies)
  mutable bool m_writeOverrides = false;
  // The overrides read from a scene file, by entity, until load() has brought the instances up to date
  std::vector<std::pair<Entity, json>> m_loadedOverrides;

  // The prefab file in the cache, read first if it is not there; with `checkChanged`, read again if it changed on
  // disk (its instances are updated then). Null if it cannot be read.
  const PrefabFile *prefabFile(const std::string &resolvedPath, bool checkChanged);
  const PrefabFile *cachedPrefabFile(const std::string &resolvedPath) const;
  static void fillPrefabFile(PrefabFile &file);
  // The entities of an instance with their place in the prefab file, the root first
  std::vector<std::pair<int, Entity>> collectParts(Entity root) const;
  json liveParts(Entity root, const std::vector<std::pair<int, Entity>> &parts) const;
  json livePart(Entity entity, Entity root, const std::map<Entity, int> &indexOf) const;
  static json applyOverrides(const json &base, const json &patch);
  // Makes the instance's entities match `target` (parts by index), in place
  void syncInstance(Entity root, const json &target);
  // Replaces a prefab's baseline and brings every instance but `skip` to it, keeping their overrides
  size_t updateInstances(const std::string &resolvedPath, PrefabFile fresh, Entity skip);
  json instanceOverridesPatch(Entity root) const;
  void linkEntities(Entity root, const std::string &path, const std::vector<Entity> &entities);
  void syncLoadedInstances();

  void registerBuiltIns();
  // entities as a file's "entities" list; `root` (a prefab's) is written without its parent, at the origin, and
  // without links to prefabs. `written` gets the entities that made it into the list, in order.
  json entitiesToJson(const std::vector<Entity> &entities, Entity root, std::vector<Entity> *written = nullptr) const;
  bool writeFile(const std::string &path, json entities, bool withLayers = false) const;
  std::optional<json> readFile(const std::string &path, bool log = true) const;
  std::vector<Entity> createEntities(const json &entries);
  std::optional<json> saveModel(Entity entity) const;
  void loadModel(Entity entity, const json &data);
};
