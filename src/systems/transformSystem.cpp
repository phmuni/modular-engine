// Model matrices from Transforms, and keeping children placed relative to their parents.

#include "systems/transformSystem.h"

#include "components/parent.h"
#include "foundation/core/logger.h"
#include "glm/ext/matrix_transform.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

glm::mat4 TransformSystem::calculateModelMatrix(const Transform &transform) {
  glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), transform.position);

  glm::quat qx = glm::angleAxis(glm::radians(transform.rotation.x), glm::vec3(1, 0, 0));
  glm::quat qy = glm::angleAxis(glm::radians(transform.rotation.y), glm::vec3(0, 1, 0));
  glm::quat qz = glm::angleAxis(glm::radians(transform.rotation.z), glm::vec3(0, 0, 1));

  glm::quat rotationQuat = qy * qx * qz;

  modelMatrix *= glm::mat4_cast(rotationQuat);
  modelMatrix = glm::scale(modelMatrix, transform.scale);

  return modelMatrix;
}

Transform TransformSystem::decompose(const glm::mat4 &matrix) {
  Transform result;
  result.position = glm::vec3(matrix[3]);
  result.scale = glm::vec3(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
                           glm::length(glm::vec3(matrix[2])));
  if (result.scale.x <= 0.0f || result.scale.y <= 0.0f || result.scale.z <= 0.0f)
    return result; // collapsed to nothing: no rotation to read
  glm::mat4 rotation(1.0f);
  for (int axis = 0; axis < 3; ++axis)
    rotation[axis] = glm::vec4(glm::vec3(matrix[axis]) / result.scale[axis], 0.0f);
  result.rotation = eulerDegrees(glm::quat_cast(glm::mat3(rotation)));
  return result;
}

glm::vec3 TransformSystem::eulerDegrees(const glm::quat &rotation) {
  // calculateModelMatrix rotates by Y, then X, then Z (qy * qx * qz), which extractEulerAngleYXZ undoes. In double:
  // the split below must see the rotation, not float noise of its own making.
  const glm::dmat4 matrix = glm::mat4_cast(glm::normalize(glm::dquat(rotation)));
  double yaw = 0.0, pitch = 0.0, roll = 0.0;
  // Under 1e-4 (a pitch within 0.006 degrees of 90) a float rotation's own noise is all there is between yaw and
  // roll: the roll is 0 and the yaw takes the whole turn, read from the X axis, which Ry * Rx(+-90) leaves at
  // (cos yaw, 0, -sin yaw)
  const double pitchCos = std::sqrt(matrix[0][1] * matrix[0][1] + matrix[1][1] * matrix[1][1]);
  if (pitchCos < 1e-4) {
    pitch = std::atan2(-matrix[2][1], pitchCos);
    yaw = std::atan2(-matrix[0][2], matrix[0][0]);
  } else {
    glm::extractEulerAngleYXZ(matrix, yaw, pitch, roll);
  }
  const auto clean = [](double radians) {
    const float degrees = static_cast<float>(std::round(glm::degrees(radians) * 1000.0) / 1000.0);
    return degrees == 0.0f ? 0.0f : degrees; // not -0
  };
  return glm::vec3(clean(pitch), clean(yaw), clean(roll));
}

namespace {
// How many parents up the entity has; -1 for a loop (which setParent refuses, but a scene file could hold)
int depthOf(ComponentManager &componentManager, Entity entity) {
  int depth = 0;
  for (const Parent *parent = componentManager.tryGet<Parent>(entity); parent;
       parent = componentManager.tryGet<Parent>(parent->entity)) {
    if (++depth > 256)
      return -1;
  }
  return depth;
}
} // namespace

void TransformSystem::updateHierarchy(ComponentManager &componentManager) {
  // Parents first: a child reads its parent's Transform after the parent was placed this frame
  std::vector<std::pair<int, Entity>> children;
  std::vector<Entity> orphans;
  componentManager.forEachComponent<Parent>([&](Entity entity, Parent &parent) {
    if (!componentManager.has<Transform>(parent.entity) || !componentManager.has<Transform>(entity)) {
      orphans.push_back(entity); // the parent is gone (or never was): the child stays where it is
      return;
    }
    const int depth = depthOf(componentManager, entity);
    if (depth < 0) {
      LOG_W("[TransformSystem] Entity %d is its own ancestor; it no longer follows %d", entity, parent.entity);
      orphans.push_back(entity);
      return;
    }
    children.emplace_back(depth, entity);
  });
  for (Entity entity : orphans)
    componentManager.removeComponent<Parent>(entity);
  std::sort(children.begin(), children.end());

  for (const auto &[depth, entity] : children) {
    Parent &parent = componentManager.get<Parent>(entity);
    Transform &transform = componentManager.get<Transform>(entity);
    const Transform &parentTransform = componentManager.get<Transform>(parent.entity);

    if (transform != parent.lastWorld) {
      // Moved in the world since the last update: it stays there, and its place relative to the parent follows
      parent.local = decompose(glm::inverse(calculateModelMatrix(parentTransform)) * calculateModelMatrix(transform));
    } else if (parent.local == parent.lastLocal && parentTransform == parent.lastParent) {
      continue; // nothing changed: not even recomputed, so values do not drift
    } else {
      transform = decompose(calculateModelMatrix(parentTransform) * calculateModelMatrix(parent.local));
    }
    parent.lastWorld = transform;
    parent.lastLocal = parent.local;
    parent.lastParent = parentTransform;
  }
}

bool TransformSystem::setParent(ComponentManager &componentManager, Entity child, Entity parent) {
  if (parent == -1) {
    componentManager.removeComponent<Parent>(child); // its Transform already is its place in the world
    return true;
  }
  if (!componentManager.has<Transform>(child) || !componentManager.has<Transform>(parent)) {
    LOG_W("[TransformSystem] setParent(%d, %d): both need a Transform", child, parent);
    return false;
  }
  for (Entity ancestor = parent; ancestor != -1; ancestor = getParent(componentManager, ancestor)) {
    if (ancestor == child) {
      LOG_W("[TransformSystem] setParent(%d, %d): %d is the child itself or one of its descendants", child, parent,
            parent);
      return false;
    }
  }
  const Transform &world = componentManager.get<Transform>(child);
  const Transform &parentWorld = componentManager.get<Transform>(parent);
  Parent link;
  link.entity = parent;
  link.local = decompose(glm::inverse(calculateModelMatrix(parentWorld)) * calculateModelMatrix(world));
  link.lastWorld = world;
  link.lastLocal = link.local;
  link.lastParent = parentWorld;
  if (Parent *existing = componentManager.tryGet<Parent>(child))
    *existing = link;
  else
    componentManager.addInPlace<Parent>(child) = link;
  return true;
}

void TransformSystem::setLocalTransform(ComponentManager &componentManager, Entity child, const Transform &local) {
  Parent *parent = componentManager.tryGet<Parent>(child);
  Transform *transform = componentManager.tryGet<Transform>(child);
  if (!transform)
    return;
  if (!parent || !componentManager.has<Transform>(parent->entity)) {
    *transform = local; // no parent: local is the world
    return;
  }
  const Transform &parentWorld = componentManager.get<Transform>(parent->entity);
  parent->local = local;
  *transform = decompose(calculateModelMatrix(parentWorld) * calculateModelMatrix(local));
  parent->lastWorld = *transform;
  parent->lastLocal = local;
  parent->lastParent = parentWorld;
}

Entity TransformSystem::getParent(ComponentManager &componentManager, Entity entity) const {
  const Parent *parent = componentManager.tryGet<Parent>(entity);
  return parent ? parent->entity : -1;
}

std::vector<Entity> TransformSystem::getChildren(ComponentManager &componentManager, Entity entity) const {
  std::vector<Entity> children;
  componentManager.forEachComponent<Parent>([&](Entity child, Parent &parent) {
    if (parent.entity == entity)
      children.push_back(child);
  });
  std::sort(children.begin(), children.end()); // creation order
  return children;
}

std::vector<Entity> TransformSystem::getDescendants(ComponentManager &componentManager, Entity entity) const {
  std::vector<Entity> descendants = getChildren(componentManager, entity);
  for (size_t i = 0; i < descendants.size(); ++i) { // grows while it is read: breadth first
    if (descendants.size() > 100000)
      break; // a loop in a hand-written file
    for (Entity grandchild : getChildren(componentManager, descendants[i]))
      descendants.push_back(grandchild);
  }
  return descendants;
}
