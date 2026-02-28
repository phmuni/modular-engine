#pragma once
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

class BaseSystem {
public:
  virtual ~BaseSystem() = default;
};

class SystemManager {
private:
  std::unordered_map<std::type_index, std::unique_ptr<BaseSystem>> m_systems;

public:
  template <typename T, typename... Args> void insert(Args &&...args);
  template <typename T> T &getSystem() const;
};

template <typename T, typename... Args> void SystemManager::insert(Args &&...args) {
  std::type_index typeId(typeid(T));
  auto system = std::make_unique<T>(std::forward<Args>(args)...);
  m_systems[typeId] = std::move(system);
}

template <typename T> T &SystemManager::getSystem() const {
  auto it = m_systems.find(std::type_index(typeid(T)));
  if (it != m_systems.end()) {
    return *static_cast<T *>(it->second.get());
  }
  throw std::runtime_error("System not found");
}
