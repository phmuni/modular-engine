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
#include "systems/prefabFileEdit.h"
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
  // Entities the game created while it ran (EntityManager::isRuntime) are left out, unless `includeRuntime`: the game's
  // own picture of what it built (--save-scene) keeps them, the editor's Save Scene does not.
  bool save(const std::string &path, bool includeRuntime = false) const;
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
  //
  // Nested prefabs: a prefab file can hold instances of other prefabs. Such an entry in the file has only the
  // nested root's Transform, its Parent and a "PrefabInstance": {"path", "overrides"}: the inner file's path and the
  // changes this prefab makes to it (a JSON patch, as in scenes); the inner file's own entities are not copied. An
  // instance loads them from the inner file, so editing the inner file updates every outer instance (and every
  // scene) that does not override what changed. The parts an instance is made of are addressed by a key: "3" for the
  // 4th entity of its own file, "3.1" for the 2nd entity of the prefab nested at entry 3, "3.1.0" one level deeper.
  // Overrides are patches over those keys, so layers stack: the inner file, then the outer prefab's overrides of it,
  // then the scene instance's (its own diff against the outer file, with the nested keys), the last one winning.
  // Prefabs that contain themselves, directly or through others, are refused (savePrefab/applyInstance fail,
  // instantiate returns nothing). A nested file that cannot be read leaves its entities as they are.
  // Not covered: entities added inside a nested instance are not kept by the outer prefab (a warning is logged when
  // saving), and files with several roots (not linked).

  // The root of the linked instance `entity` belongs to (itself, if it is one), or -1
  Entity instanceRoot(Entity entity) const;
  // The linked instance roots whose file is `path` (resolved)
  std::vector<Entity> instancesOf(const std::string &resolvedPath) const;
  // Whether the instance's file exists on disk
  bool prefabFileExists(Entity root) const;
  // The instance's overrides as the paths they change ("Wheel/Model/opacity"), empty when it matches its file
  std::vector<std::string> instanceOverrides(Entity root);
  // The instance a nested instance's `root` is part of (a house for its door), or -1 when it stands alone (also when
  // the outer file is missing)
  Entity enclosingInstance(Entity root) const;
  // Writes the instance (the root with all its descendants) to its file, so every instance follows it, and links
  // the entities to it. `path` is another file to write instead (Save as prefab): the instance is then linked to it.
  // Apply always writes the file of the instance `root` is the root of: on a nested root that is the inner file
  // (door.json), with the outer one untouched; on the outer root it is the outer file (house.json), where the
  // nested instances are written as references and whatever differs from their files becomes the outer prefab's
  // override of them. With `path` on a nested root the entities are only written (not linked), as savePrefab does.
  bool applyInstance(Entity root, const std::string &path = {});
  // Drops the overrides: the instance matches its file again (the root keeps its place). On a nested root that is
  // the outer file's view of it (the inner file plus the outer prefab's overrides of it); on an outer root,
  // everything below it too.
  void revertInstance(Entity root);
  // Breaks the link of this level: plain entities from now on. Instances nested inside stay linked to their own
  // files (on their own now); a nested instance unpacked stays in place and the outer instance counts it as removed.
  void unpackInstance(Entity root);
  // Rereads the prefab files in use that changed on disk and updates their instances; returns how many it updated.
  // Only a stat per file, but it is not free: the editor calls it a few times a second while it is open.
  size_t refreshPrefabs();
  // Undo of prefab files. While a journal is on, applyInstance and savePrefab note what each file held before they
  // wrote it; endFileJournal returns those files (first content before, content at the end after) and switches it
  // off. Engine::beginCheckpoint/endCheckpoint use it, so an undo step also covers the prefab files it wrote.
  void beginFileJournal();
  std::vector<PrefabFileEdit> endFileJournal();
  // Puts a prefab file back as the journal saw it (nothing: deletes it) and makes the cache match, without touching
  // the instances: undo and redo restore those with the scene
  bool restorePrefabFile(const std::string &resolvedPath, const std::optional<std::string> &content);
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
    json parts;                             // by key ("0", "1", "2.0"...): what an instance is compared with, with the
                                            // nested prefabs expanded (their overrides in it applied)
    int root = -1;                          // the one entity without a parent; -1 if there is not exactly one
    std::filesystem::file_time_type time{}; // when it was read
    std::uintmax_t size = 0;                // and its size then: two writes in one clock tick share a time
    bool exists = true;
    std::vector<std::string> opaque;  // keys of nested instances that could not be expanded: left as they are
    std::vector<std::string> deps;    // the files nested in it, however deep (keys as fileKey)
    std::vector<std::string> missing; // the ones of those that do not exist
    bool cyclic = false;              // it contains itself, directly or through others
  };
  struct Baseline {
    json parts;
    std::vector<std::string> opaque;
    bool ok = false;
  };
  using PartList = std::vector<std::pair<std::string, Entity>>; // key and entity, the root first
  // By resolved path. Mutable: saving a scene (const) may find a file not read yet.
  mutable std::unordered_map<std::string, PrefabFile> m_prefabFiles;
  // Set while save() writes a scene: PrefabInstance then holds the overrides too (not wanted in undo copies)
  mutable bool m_writeOverrides = false;
  bool m_journaling = false;
  mutable std::vector<PrefabFileEdit> m_journal; // mutable: savePrefab is const
  // Notes the file's content before a write, while the journal is on (once per file)
  void noteFileBefore(const std::string &resolvedPath) const;
  // The overrides read from a scene file, by entity, until load() has brought the instances up to date
  std::vector<std::pair<Entity, json>> m_loadedOverrides;

  // Files whose parts are out of date (an inner file changed): filled again when next asked for
  mutable std::unordered_set<std::string> m_stale;
  mutable std::vector<std::string> m_resolving; // files being filled, to find a prefab that contains itself

  // The prefab file in the cache, read first if it is not there; with `checkChanged`, read again if it changed on
  // disk (its instances are updated then), the files nested in it too. Null if it cannot be read.
  const PrefabFile *prefabFile(const std::string &resolvedPath, bool checkChanged);
  const PrefabFile *cachedPrefabFile(const std::string &resolvedPath) const;
  void fillPrefabFile(PrefabFile &file, const std::string &key) const;
  void expandNested(PrefabFile &file, size_t index, json &part, const json &link) const;
  // Reads the changed ones of `keys` again (and those with a nested file that appeared) and updates their instances
  size_t refreshKeys(const std::vector<std::string> &keys, bool log);
  void refillDependents(const std::string &key);
  // The instances of a file that nothing else holds (not nested in another instance whose file is there)
  std::vector<Entity> topInstancesOf(const std::string &key) const;
  // The entities of an instance with their key, the root first
  PartList collectParts(Entity root) const;
  json liveParts(Entity root, const PartList &parts, const std::vector<std::string> &opaque = {}) const;
  json livePart(Entity entity, Entity root, const std::map<Entity, std::string> &keyOf) const;
  // What `root` is compared with and reverted to: its file's parts, or for a nested root the outer file's view of it
  Baseline baselineOf(Entity root) const;
  static json applyOverrides(const json &base, const json &patch);
  // Makes the instance's entities match `target` (parts by key), in place; `refresh` tells the systems that read
  // components (not needed for entities that are new anyway)
  void syncInstance(Entity root, const json &target, const std::vector<std::string> &opaque, bool refresh = true);
  // Replaces the baseline of the given files (key -> new content) and of the files that contain them, and brings
  // every instance but `skip` to it, keeping their overrides
  size_t updateInstances(std::map<std::string, PrefabFile> fresh, Entity skip);
  json instanceOverridesPatch(Entity root) const;
  // Whether a nested instance can be written as a reference (its file is readable and does not contain itself), and
  // that reference: its path and what it changed from that file
  bool canReference(Entity entity) const;
  std::optional<json> nestedLink(Entity entity) const;
  // The entities a prefab written from `root` holds, root first: nested instances are one entity each
  std::vector<Entity> prefabEntities(Entity root) const;
  // The file that would make a prefab at `targetKey` contain itself, empty if none
  std::string nestingCycle(const std::string &targetKey, Entity root, const std::vector<Entity> &entities) const;
  void linkEntities(Entity root, const std::string &path, const std::vector<Entity> &entities,
                    const std::unordered_set<Entity> &references);
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
