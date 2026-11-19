// Ray and sweep queries against model triangles, each run in the model's own space.

#include "systems/modelQueries.h"

#include "components/hidden.h"
#include "components/model.h"
#include "components/transform.h"
#include "rendering/resources/mesh.h"
#include "systems/resourceSystem.h"
#include "systems/transformSystem.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
struct Box {
  glm::vec3 min{std::numeric_limits<float>::max()};
  glm::vec3 max{-std::numeric_limits<float>::max()};
  void add(const glm::vec3 &p) {
    min = glm::min(min, p);
    max = glm::max(max, p);
  }
};

bool overlap(const Box &a, const Box &b) {
  return glm::all(glm::lessThanEqual(a.min, b.max)) && glm::all(glm::lessThanEqual(b.min, a.max));
}

bool contains(const Box &box, const glm::vec3 &p) {
  return glm::all(glm::lessThanEqual(box.min, p)) && glm::all(glm::lessThanEqual(p, box.max));
}

// The box around `box` after a transform (its 8 corners)
Box transformBox(const Box &box, const glm::mat4 &matrix) {
  Box result;
  for (int i = 0; i < 8; ++i) {
    const glm::vec3 corner(i & 1 ? box.max.x : box.min.x, i & 2 ? box.max.y : box.min.y,
                           i & 4 ? box.max.z : box.min.z);
    result.add(glm::vec3(matrix * glm::vec4(corner, 1.0f)));
  }
  return result;
}

Box meshBox(const Mesh &mesh) {
  Box box;
  box.min = mesh.getBoundsMin();
  box.max = mesh.getBoundsMax();
  return box;
}

Box triangleBox(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
  Box box;
  box.add(a);
  box.add(b);
  box.add(c);
  return box;
}

// Möller-Trumbore, either side: where the line o + t * d crosses the triangle (t in units of d)
bool rayTriangle(const glm::vec3 &o, const glm::vec3 &d, const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c,
                 float &t) {
  constexpr float kEdge = 1e-5f; // counts hits on an edge, so a ray down a shared edge is not lost between faces
  const glm::vec3 e1 = b - a, e2 = c - a;
  const glm::vec3 p = glm::cross(d, e2);
  const float det = glm::dot(e1, p);
  if (std::abs(det) < 1e-14f)
    return false; // parallel to the triangle
  const float inverse = 1.0f / det;
  const glm::vec3 s = o - a;
  const float u = glm::dot(s, p) * inverse;
  if (u < -kEdge || u > 1.0f + kEdge)
    return false;
  const glm::vec3 q = glm::cross(s, e1);
  const float v = glm::dot(d, q) * inverse;
  if (v < -kEdge || u + v > 1.0f + kEdge)
    return false;
  t = glm::dot(e2, q) * inverse;
  return true;
}

// Slabs: where the ray enters the box, if it does within maxT
bool rayBox(const glm::vec3 &o, const glm::vec3 &d, const Box &box, float maxT, float &enter) {
  enter = 0.0f;
  float exit = maxT;
  for (int axis = 0; axis < 3; ++axis) {
    if (std::abs(d[axis]) < 1e-12f) {
      if (o[axis] < box.min[axis] || o[axis] > box.max[axis])
        return false;
      continue;
    }
    float t0 = (box.min[axis] - o[axis]) / d[axis];
    float t1 = (box.max[axis] - o[axis]) / d[axis];
    if (t0 > t1)
      std::swap(t0, t1);
    enter = std::max(enter, t0);
    exit = std::min(exit, t1);
    if (enter > exit)
      return false;
  }
  return true;
}

// A model-space normal to world space (inverse transpose, for non-uniform scale)
glm::vec3 worldNormal(const glm::mat4 &model, const glm::vec3 &localNormal) {
  return glm::normalize(glm::transpose(glm::inverse(glm::mat3(model))) * localNormal);
}
} // namespace

std::optional<ModelHit> ModelQueries::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                              const Filter &accept) const {
  if (glm::length(direction) <= 0.0f)
    return std::nullopt;
  const glm::vec3 dir = glm::normalize(direction);

  std::optional<ModelHit> best;
  float bestT = maxDistance;
  glm::vec3 bestLocalNormal(0.0f);
  glm::mat4 bestModel(1.0f);

  m_componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
    const auto *transform = m_componentManager.tryGet<Transform>(entity);
    if (!transform || m_componentManager.has<Hidden>(entity) || (accept && !accept(entity)))
      return;
    const Mesh &mesh = m_resourceSystem.getMesh(model.meshHandle);
    const glm::mat4 toWorld = m_transformSystem.calculateModelMatrix(*transform);
    const glm::mat4 toLocal = glm::inverse(toWorld);
    // In model space the distance along the ray is the same, since the mapping is linear
    const glm::vec3 localOrigin = glm::vec3(toLocal * glm::vec4(origin, 1.0f));
    const glm::vec3 localDir = glm::vec3(toLocal * glm::vec4(dir, 0.0f));
    float enter = 0.0f;
    if (!rayBox(localOrigin, localDir, meshBox(mesh), bestT, enter))
      return;

    const auto &vertices = mesh.getCollisionVertices();
    const auto &indices = mesh.getCollisionIndices();
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      const glm::vec3 &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
      float t = 0.0f;
      if (rayTriangle(localOrigin, localDir, a, b, c, t) && t >= 0.0f && t < bestT) {
        bestT = t;
        bestLocalNormal = glm::cross(b - a, c - a);
        bestModel = toWorld;
        best = ModelHit{entity, t, glm::vec3(0.0f), glm::vec3(0.0f)};
      }
    }
  });

  if (best) {
    best->point = origin + dir * bestT;
    best->normal = worldNormal(bestModel, bestLocalNormal);
    if (glm::dot(best->normal, dir) > 0.0f)
      best->normal = -best->normal; // facing the ray, whichever way the triangle winds
  }
  return best;
}

SweepResult ModelQueries::sweep(Entity moving, const Transform &from, const glm::vec3 &delta,
                                const Filter &accept) const {
  SweepResult result;
  const auto *movingModel = m_componentManager.tryGet<Model>(moving);
  const float length = glm::length(delta);
  if (!movingModel || length <= 0.0f)
    return result;

  const Mesh &movingMesh = m_resourceSystem.getMesh(movingModel->meshHandle);
  const glm::mat4 movingToWorld = m_transformSystem.calculateModelMatrix(from);
  const glm::mat4 movingToLocal = glm::inverse(movingToWorld);
  const auto &movingVertices = movingMesh.getCollisionVertices();
  const auto &movingIndices = movingMesh.getCollisionIndices();

  // The space the move covers: the model's box at the start and at the end
  const Box startBox = transformBox(meshBox(movingMesh), movingToWorld);
  Box swept = startBox;
  swept.add(startBox.min + delta);
  swept.add(startBox.max + delta);
  swept.min -= glm::vec3(1e-3f);
  swept.max += glm::vec3(1e-3f);

  std::vector<glm::vec3> movingWorld(movingVertices.size());
  for (size_t i = 0; i < movingVertices.size(); ++i)
    movingWorld[i] = glm::vec3(movingToWorld * glm::vec4(movingVertices[i], 1.0f));
  // The obstacle seen from the moving model goes the other way
  const glm::vec3 reverseLocal = glm::vec3(movingToLocal * glm::vec4(-delta, 0.0f));
  // Hits a hair behind the start still count: a model resting on a surface touches it within rounding
  const float behind = -1e-4f / length;

  std::vector<size_t> candidates;
  m_componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
    const auto *transform = m_componentManager.tryGet<Transform>(entity);
    if (entity == moving || !transform || m_componentManager.has<Hidden>(entity) || (accept && !accept(entity)))
      return;
    const Mesh &mesh = m_resourceSystem.getMesh(model.meshHandle);
    const glm::mat4 toWorld = m_transformSystem.calculateModelMatrix(*transform);
    if (!overlap(transformBox(meshBox(mesh), toWorld), swept))
      return;
    const glm::mat4 toLocal = glm::inverse(toWorld);
    const Box sweptLocal = transformBox(swept, toLocal);
    const glm::vec3 deltaLocal = glm::vec3(toLocal * glm::vec4(delta, 0.0f));
    const auto &vertices = mesh.getCollisionVertices();
    const auto &indices = mesh.getCollisionIndices();

    // The obstacle's triangles within reach of the move
    candidates.clear();
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
      if (overlap(triangleBox(vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]]), sweptLocal))
        candidates.push_back(i);

    // The moving model's corners running into the obstacle's faces, entering them from outside
    for (const glm::vec3 &worldVertex : movingWorld) {
      const glm::vec3 p = glm::vec3(toLocal * glm::vec4(worldVertex, 1.0f));
      for (size_t i : candidates) {
        const glm::vec3 &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
        const glm::vec3 normal = glm::cross(b - a, c - a);
        if (glm::dot(deltaLocal, normal) >= 0.0f)
          continue;
        float t = 0.0f;
        if (rayTriangle(p, deltaLocal, a, b, c, t) && t >= behind && t < result.fraction) {
          result.fraction = std::max(t, 0.0f);
          result.blocker = entity;
          result.normal = worldNormal(toWorld, normal);
        }
      }
    }

    // The obstacle's corners running into the moving model's faces (a pillar's edge against a wall that moves)
    for (size_t v = 0; v < vertices.size(); ++v) {
      if (!contains(sweptLocal, vertices[v]))
        continue;
      const glm::vec3 q = glm::vec3(movingToLocal * (toWorld * glm::vec4(vertices[v], 1.0f)));
      for (size_t i = 0; i + 2 < movingIndices.size(); i += 3) {
        const glm::vec3 &a = movingVertices[movingIndices[i]], &b = movingVertices[movingIndices[i + 1]],
                        &c = movingVertices[movingIndices[i + 2]];
        const glm::vec3 normal = glm::cross(b - a, c - a);
        if (glm::dot(reverseLocal, normal) >= 0.0f)
          continue;
        float t = 0.0f;
        if (rayTriangle(q, reverseLocal, a, b, c, t) && t >= behind && t < result.fraction) {
          result.fraction = std::max(t, 0.0f);
          result.blocker = entity;
          result.normal = -worldNormal(movingToWorld, normal);
        }
      }
    }
  });
  return result;
}

bool ModelQueries::intersects(Entity moving, const Transform &pose, Entity other) const {
  const auto *movingModel = m_componentManager.tryGet<Model>(moving);
  const auto *otherModel = m_componentManager.tryGet<Model>(other);
  const auto *otherTransform = m_componentManager.tryGet<Transform>(other);
  if (!movingModel || !otherModel || !otherTransform)
    return false;
  const Mesh &movingMesh = m_resourceSystem.getMesh(movingModel->meshHandle);
  const Mesh &otherMesh = m_resourceSystem.getMesh(otherModel->meshHandle);
  const glm::mat4 movingToWorld = m_transformSystem.calculateModelMatrix(pose);
  const glm::mat4 otherToWorld = m_transformSystem.calculateModelMatrix(*otherTransform);
  if (!overlap(transformBox(meshBox(movingMesh), movingToWorld), transformBox(meshBox(otherMesh), otherToWorld)))
    return false;

  // Everything in the other model's space
  const glm::mat4 movingToOther = glm::inverse(otherToWorld) * movingToWorld;
  const auto &movingVertices = movingMesh.getCollisionVertices();
  const auto &movingIndices = movingMesh.getCollisionIndices();
  std::vector<glm::vec3> moved(movingVertices.size());
  for (size_t i = 0; i < movingVertices.size(); ++i)
    moved[i] = glm::vec3(movingToOther * glm::vec4(movingVertices[i], 1.0f));
  const Box movedBox = transformBox(meshBox(movingMesh), movingToOther);

  const auto &vertices = otherMesh.getCollisionVertices();
  const auto &indices = otherMesh.getCollisionIndices();
  std::vector<size_t> nearby;
  for (size_t i = 0; i + 2 < indices.size(); i += 3)
    if (overlap(triangleBox(vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]]), movedBox))
      nearby.push_back(i);
  if (nearby.empty())
    return false;

  // An edge of one strictly through a face of the other (touching does not count)
  auto edgeThrough = [](const glm::vec3 &from, const glm::vec3 &to, const glm::vec3 &a, const glm::vec3 &b,
                        const glm::vec3 &c) {
    float t = 0.0f;
    return rayTriangle(from, to - from, a, b, c, t) && t > 1e-4f && t < 1.0f - 1e-4f;
  };
  for (size_t m = 0; m + 2 < movingIndices.size(); m += 3) {
    const glm::vec3 &p0 = moved[movingIndices[m]], &p1 = moved[movingIndices[m + 1]], &p2 = moved[movingIndices[m + 2]];
    for (size_t i : nearby) {
      const glm::vec3 &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
      if (!overlap(triangleBox(p0, p1, p2), triangleBox(a, b, c)))
        continue;
      if (edgeThrough(p0, p1, a, b, c) || edgeThrough(p1, p2, a, b, c) || edgeThrough(p2, p0, a, b, c) ||
          edgeThrough(a, b, p0, p1, p2) || edgeThrough(b, c, p0, p1, p2) || edgeThrough(c, a, p0, p1, p2))
        return true;
    }
  }
  return false;
}
