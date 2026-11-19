#pragma once
// Saves the entities of the running scene to a JSON file and loads them back, replacing what is there.
//
// Each component is written under a key ("Transform", "Model"...) by a registered serializer; the engine registers
// its own, and games register theirs (registerComponent<T>) to save their components too. Components without a
// serializer are left out of the file, and keys in a file that nothing registered are skipped with a warning.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

class SceneSerializer : public BaseSystem {
public:
  using json = nlohmann::json;
  // Returns nothing when the entity does not have the component
  using SaveFn = std::function<std::optional<json>(Entity)>;
  using LoadFn = std::function<void(Entity, const json &)>;

  SceneSerializer(EntityManager &em, ComponentManager &cm, SystemManager &sm);

  // Components load in registration order, so one can rely on those registered before it (Transform comes first)
  void registerComponent(const std::string &key, SaveFn save, LoadFn load);

  // For plain data with to_json/from_json (e.g. NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT)
  template <typename T> void registerComponent(const std::string &key) {
    registerComponent(
        key,
        [this](Entity entity) -> std::optional<json> {
          if (const T *component = m_componentManager.tryGet<T>(entity))
            return json(*component);
          return std::nullopt;
        },
        [this](Entity entity, const json &data) { m_componentManager.addInPlace<T>(entity) = data.get<T>(); });
  }

  bool save(const std::string &path) const;
  // Reads the whole file first; if it cannot be read, the current scene stays
  bool load(const std::string &path);
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
  };

  EntityManager &m_entityManager;
  ComponentManager &m_componentManager;
  SystemManager &m_systemManager;
  std::vector<Serializer> m_serializers;
  mutable std::unordered_set<std::string> m_warnedKeys;

  void registerBuiltIns();
  std::optional<json> saveModel(Entity entity) const;
  void loadModel(Entity entity, const json &data);
};
