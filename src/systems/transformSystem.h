#pragma once
// Model matrix computation from Transform.

#include "components/transform.h"
#include "foundation/ecs/systemManager.h"

class TransformSystem : public BaseSystem {
public:
  TransformSystem() = default;
  glm::mat4 calculateModelMatrix(const Transform &transform);
};
