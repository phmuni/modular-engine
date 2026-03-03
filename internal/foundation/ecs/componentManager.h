#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>

using Entity = int;

class ComponentManager {
private:
  using ErasedPtr = std::unique_ptr<void, void (*)(void *)>;

  template <typename T> static ErasedPtr makeErased(T *ptr) {
    return ErasedPtr(ptr, [](void *p) { delete static_cast<T *>(p); });
  }

  std::unordered_map<std::type_index, std::unordered_map<Entity, ErasedPtr>> m_storage;

public:
  template <typename T> void insert(Entity entity, std::unique_ptr<T> component) {
    T *raw = component.release();
    auto &inner = m_storage[std::type_index(typeid(T))];
    inner.insert_or_assign(entity, makeErased<T>(raw));
  }

  template <typename T, typename... Args> T &add(Entity entity, Args &&...args) {
    T *raw = new T(std::forward<Args>(args)...);
    T &ref = *raw;
    auto &inner = m_storage[std::type_index(typeid(T))];
    inner.insert_or_assign(entity, makeErased<T>(raw));
    return ref;
  }

  template <typename T> T &get(Entity entity) {
    auto &map = m_storage.at(std::type_index(typeid(T)));
    auto it = map.find(entity);
    if (it == map.end()) {
      throw std::runtime_error("Component not found for entity");
    }
    return *static_cast<T *>(it->second.get());
  }

  template <typename T> T *tryGet(Entity entity) {
    auto it = m_storage.find(std::type_index(typeid(T)));
    if (it == m_storage.end())
      return nullptr;

    auto cit = it->second.find(entity);
    if (cit == it->second.end())
      return nullptr;

    return static_cast<T *>(cit->second.get());
  }

  template <typename T> bool has(Entity entity) {
    auto it = m_storage.find(std::type_index(typeid(T)));
    if (it == m_storage.end())
      return false;
    return it->second.find(entity) != it->second.end();
  }

  template <typename T> Entity findEntityWith() {
    auto it = m_storage.find(std::type_index(typeid(T)));
    if (it == m_storage.end() || it->second.empty())
      return -1;
    return it->second.begin()->first;
  }

  void removeAll(Entity entity) {
    for (auto &[_, map] : m_storage)
      map.erase(entity);
  }

  template <typename T> void remove(Entity entity) { m_storage[std::type_index(typeid(T))].erase(entity); }

  template <typename T> void each(std::function<void(Entity, T &)> fn) {
    auto it = m_storage.find(std::type_index(typeid(T)));
    if (it == m_storage.end())
      return;
    for (auto &[entity, ptr] : it->second) {
      fn(entity, *static_cast<T *>(ptr.get()));
    }
  }
};
