#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>

class BaseSystem {
public:
  virtual ~BaseSystem() = default;
};

class SystemManager {
private:
  std::unordered_map<std::type_index, std::unique_ptr<BaseSystem>> systems;

public:
  template <typename T, typename... Args> void registerSystem(Args &&...args);

  template <typename T> T &getSystem() const;

  template <typename T> void updateAll() const;
};

template <typename T, typename... Args> void SystemManager::registerSystem(Args &&...args) {
  std::type_index typeId(typeid(T));

  auto system = std::make_unique<T>(std::forward<Args>(args)...);
  systems[typeId] = std::move(system);
}

template <typename T> T &SystemManager::getSystem() const {
  auto it = systems.find(typeid(T));
  if (it != systems.end()) {
    return *static_cast<T *>(it->second.get());
  }
  std::string error = std::string("System ") + typeid(T).name() + " not found";
  throw std::runtime_error(error);
}

template <typename T> void SystemManager::updateAll() const {}
