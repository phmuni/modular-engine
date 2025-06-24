#pragma once
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>


class SystemManager {
private:
  std::unordered_map<std::type_index, std::shared_ptr<void>> systems;

public:
  template <typename T, typename... Args> void registerSystem(Args &&...args);

  template <typename T> T &getSystem() const;

  template <typename T> void updateAll() const;
};

template <typename T, typename... Args> void SystemManager::registerSystem(Args &&...args) {
  std::type_index typeId(typeid(T));

  auto system = std::make_shared<T>(std::forward<Args>(args)...);
  systems[typeId] = system;
}

template <typename T> T &SystemManager::getSystem() const {
  std::type_index typeId(typeid(T));
  auto it = systems.find(typeId);
  if (it != systems.end()) {
    return *std::static_pointer_cast<T>(it->second);
  }

  throw std::runtime_error("System not found");
}

template <typename T> void SystemManager::updateAll() const {
  for (const auto &pair : systems) {
    auto system = std::dynamic_pointer_cast<T>(pair.second);
    if (system) {
      system->update(); // Call the update method of the system
    }
  }
}
