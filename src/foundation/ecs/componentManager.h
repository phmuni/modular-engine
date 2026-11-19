#pragma once
// Component storage for the ECS: one sparse-set pool per component type.
//
// Each pool keeps a dense array of (entity, component*) for iteration and a sparse entity -> dense index table for
// O(1) lookup. Components live in fixed-size chunks, so their addresses never change: references stay valid while
// other components of the same type are added (iterating a pool while adding to it is safe). Removing during
// iteration of the same pool skips the element swapped into the removed slot, so defer removals until after the loop.

#include "entityManager.h"
#include "foundation/core/logger.h"
#include "foundation/core/typeName.h"
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <typeinfo>
#include <utility>
#include <vector>

namespace ecs_detail {

using ComponentTypeId = size_t;

inline ComponentTypeId nextComponentTypeId() {
  static ComponentTypeId next = 0;
  return next++;
}

// A small, dense id per component type, so pools are found by index instead of hashing a type_index
template <typename T> ComponentTypeId componentTypeId() {
  static const ComponentTypeId id = nextComponentTypeId();
  return id;
}

class IComponentPool {
public:
  virtual ~IComponentPool() = default;
  virtual bool contains(Entity entity) const = 0;
  virtual void remove(Entity entity) = 0;
};

template <typename T> class ComponentPool final : public IComponentPool {
public:
  ComponentPool() = default;
  ComponentPool(const ComponentPool &) = delete;
  ComponentPool &operator=(const ComponentPool &) = delete;

  ~ComponentPool() override {
    for (T *component : m_components)
      component->~T();
  }

  bool contains(Entity entity) const override { return denseIndex(entity) != kAbsent; }

  T *get(Entity entity) const {
    uint32_t index = denseIndex(entity);
    return index == kAbsent ? nullptr : m_components[index];
  }

  // Replaces an existing component in place, keeping its address
  template <typename... Args> T &emplace(Entity entity, Args &&...args) {
    if (T *existing = get(entity)) {
      existing->~T();
      return *new (existing) T{std::forward<Args>(args)...};
    }

    uint32_t slot = acquireSlot();
    T *component;
    try {
      component = new (slotAddress(slot)) T{std::forward<Args>(args)...};
    } catch (...) {
      m_freeSlots.push_back(slot);
      throw;
    }

    if (static_cast<size_t>(entity) >= m_sparse.size())
      m_sparse.resize(static_cast<size_t>(entity) + 1, kAbsent);
    m_sparse[entity] = static_cast<uint32_t>(m_entities.size());
    m_entities.push_back(entity);
    m_components.push_back(component);
    m_slots.push_back(slot);
    return *component;
  }

  void remove(Entity entity) override {
    uint32_t index = denseIndex(entity);
    if (index == kAbsent)
      return;

    m_components[index]->~T();
    m_freeSlots.push_back(m_slots[index]);

    // Swap-and-pop keeps the dense arrays packed
    uint32_t last = static_cast<uint32_t>(m_entities.size() - 1);
    if (index != last) {
      m_entities[index] = m_entities[last];
      m_components[index] = m_components[last];
      m_slots[index] = m_slots[last];
      m_sparse[m_entities[index]] = index;
    }
    m_entities.pop_back();
    m_components.pop_back();
    m_slots.pop_back();
    m_sparse[entity] = kAbsent;
  }

  // Re-reads the size every step, so components added during the loop are visited too
  template <typename Fn> void forEach(Fn &fn) {
    for (size_t i = 0; i < m_entities.size(); ++i)
      fn(m_entities[i], *m_components[i]);
  }

  Entity first() const { return m_entities.empty() ? -1 : m_entities.front(); }

private:
  static constexpr uint32_t kAbsent = UINT32_MAX;
  static constexpr uint32_t kChunkSize = 256;

  using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

  std::vector<uint32_t> m_sparse; // entity -> dense index
  std::vector<Entity> m_entities; // dense
  std::vector<T *> m_components;  // dense
  std::vector<uint32_t> m_slots;  // dense -> storage slot
  std::vector<std::unique_ptr<Storage[]>> m_chunks;
  std::vector<uint32_t> m_freeSlots;
  uint32_t m_slotCount = 0;

  uint32_t denseIndex(Entity entity) const {
    if (entity < 0 || static_cast<size_t>(entity) >= m_sparse.size())
      return kAbsent;
    return m_sparse[entity];
  }

  uint32_t acquireSlot() {
    if (!m_freeSlots.empty()) {
      uint32_t slot = m_freeSlots.back();
      m_freeSlots.pop_back();
      return slot;
    }
    if (m_slotCount == m_chunks.size() * kChunkSize)
      m_chunks.push_back(std::make_unique<Storage[]>(kChunkSize));
    return m_slotCount++;
  }

  void *slotAddress(uint32_t slot) { return &m_chunks[slot / kChunkSize][slot % kChunkSize]; }
};

} // namespace ecs_detail

class ComponentManager {
private:
  std::vector<std::unique_ptr<ecs_detail::IComponentPool>> m_pools; // indexed by component type id

  template <typename T> ecs_detail::ComponentPool<T> &pool() {
    const auto id = ecs_detail::componentTypeId<T>();
    if (id >= m_pools.size())
      m_pools.resize(id + 1);
    if (!m_pools[id])
      m_pools[id] = std::make_unique<ecs_detail::ComponentPool<T>>();
    return static_cast<ecs_detail::ComponentPool<T> &>(*m_pools[id]);
  }

  template <typename T> ecs_detail::ComponentPool<T> *findPool() const {
    const auto id = ecs_detail::componentTypeId<T>();
    if (id >= m_pools.size() || !m_pools[id])
      return nullptr;
    return static_cast<ecs_detail::ComponentPool<T> *>(m_pools[id].get());
  }

public:
  template <typename T> void insert(Entity entity, std::unique_ptr<T> component) {
    pool<T>().emplace(entity, std::move(*component));
  }

  template <typename T, typename... Args> T &addInPlace(Entity entity, Args &&...args) {
    return pool<T>().emplace(entity, std::forward<Args>(args)...);
  }

  template <typename T> T &get(Entity entity) {
    if (T *component = tryGet<T>(entity))
      return *component;
    LOG_E("[ComponentManager] Entity %d has no %s component", entity, typeName(typeid(T)).c_str());
    throw std::runtime_error("Component not found for entity");
  }

  template <typename T> T *tryGet(Entity entity) {
    auto *components = findPool<T>();
    return components ? components->get(entity) : nullptr;
  }

  template <typename T> bool has(Entity entity) {
    auto *components = findPool<T>();
    return components && components->contains(entity);
  }

  template <typename T> Entity firstEntityWithComponent() {
    auto *components = findPool<T>();
    return components ? components->first() : -1;
  }

  void removeAllComponents(Entity entity) {
    for (auto &components : m_pools)
      if (components)
        components->remove(entity);
  }

  template <typename T> void removeComponent(Entity entity) {
    if (auto *components = findPool<T>())
      components->remove(entity);
  }

  template <typename T, typename Fn> void forEachComponent(Fn &&fn) {
    if (auto *components = findPool<T>())
      components->forEach(fn);
  }
};
