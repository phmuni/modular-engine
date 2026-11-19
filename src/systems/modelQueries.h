#pragma once
// Geometry queries against the triangles of models (not their Collision boxes): rays, for picking and placing
// things on surfaces, and sweeps, for moving a model until it touches another.
//
// Every test runs in the model's own space, so rotated and scaled models are hit as they look. A box test first
// skips the models (and the triangles) a query cannot reach.

#include "foundation/ecs/componentManager.h"
#include <functional>
#include <glm/glm.hpp>
#include <limits>
#include <optional>

class ResourceSystem;
class TransformSystem;
struct Transform;

struct ModelHit {
  Entity entity;
  float distance;   // along the ray, from its origin
  glm::vec3 point;  // world space
  glm::vec3 normal; // world space, unit length, facing the ray
};

struct SweepResult {
  float fraction = 1.0f;    // of the move that can be made before touching (1: all of it)
  Entity blocker = -1;      // what stops it, or -1
  glm::vec3 normal{0.0f};   // the surface it touches, pointing back at the moving model
};

class ModelQueries {
public:
  using Filter = std::function<bool(Entity)>;

  ModelQueries(ComponentManager &componentManager, ResourceSystem &resourceSystem, TransformSystem &transformSystem)
      : m_componentManager(componentManager), m_resourceSystem(resourceSystem), m_transformSystem(transformSystem) {}

  // The nearest triangle of a visible model along the ray; `accept` skips entities it returns false for
  std::optional<ModelHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction,
                                  float maxDistance = std::numeric_limits<float>::max(),
                                  const Filter &accept = nullptr) const;

  // How far `moving`, placed at `from`, can travel along `delta` before it touches another visible model. Tests each
  // model's vertices against the other's triangles, both ways: exact for a corner or face meeting a face, the usual
  // contacts; two edges crossing (rare, with rotated boxes) can slip into each other a little.
  // Only surfaces met from outside stop it, so a model that starts inside another can move out of it.
  SweepResult sweep(Entity moving, const Transform &from, const glm::vec3 &delta,
                    const Filter &accept = nullptr) const;

  // Whether `moving`, placed at `pose`, has triangles crossing those of `other` (one inside the other without any
  // crossing does not count)
  bool intersects(Entity moving, const Transform &pose, Entity other) const;

private:
  ComponentManager &m_componentManager;
  ResourceSystem &m_resourceSystem;
  TransformSystem &m_transformSystem;
};
