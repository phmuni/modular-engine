#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>

using Entity = int;

class BaseComponent {
public:
  virtual ~BaseComponent() = default;
};

class ComponentManager {
private:
  // Type-erased storage using std::type_index as key.
  // Enables heterogeneous component types in a single container while maintaining type safety
  // through template specialization at access time.
  std::unordered_map<std::type_index, std::unordered_map<Entity, std::unique_ptr<BaseComponent>>> storage;

public:
  template <typename T> void add(Entity entity, std::unique_ptr<T> component) {
    storage[std::type_index(typeid(T))][entity] = std::move(component);
  }

  template <typename T> T &get(Entity entity) {
    auto &map = storage.at(std::type_index(typeid(T)));
    auto it = map.find(entity);
    if (it == map.end()) {
      std::string error =
          std::string("Component ") + typeid(T).name() + " not found for entity " + std::to_string(entity);
      throw std::runtime_error(error);
    }
    return *static_cast<T *>(it->second.get());
  }

  // Safe access pattern: returns nullptr instead of throwing.
  // Preferred over get() when component presence is uncertain.
  template <typename T> T *tryGet(Entity entity) {
    auto it = storage.find(std::type_index(typeid(T)));
    if (it == storage.end())
      return nullptr;
    auto componentIt = it->second.find(entity);
    if (componentIt == it->second.end())
      return nullptr;
    return static_cast<T *>(componentIt->second.get());
  }

  template <typename T> bool has(Entity entity) {
    auto it = storage.find(std::type_index(typeid(T)));
    if (it == storage.end())
      return false;
    return it->second.find(entity) != it->second.end();
  }

  template <typename T> Entity entityWithComponent() {
    auto it = storage.find(std::type_index(typeid(T)));
    if (it == storage.end())
      return -1;

    for (const auto &[entity, _] : it->second) {
      return entity;
    }

    return -1;
  }

  void removeAll(Entity entity) {
    for (auto &[_, map] : storage) {
      map.erase(entity);
    }
  }

  template <typename T> void remove(Entity entity) {
    auto &map = storage[std::type_index(typeid(T))];
    map.erase(entity);
  }
};
