#pragma once
// Model matrix computation from Transform.

#include "components/transform.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>

class TransformSystem : public BaseSystem {
public:
  TransformSystem() = default;
  glm::mat4 calculateModelMatrix(const Transform &transform);
};
