#pragma once

#include "components/transformComponent.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>

class TransformSystem : public BaseSystem {
public:
  TransformSystem() = default;
  glm::mat4 calculateModelMatrix(const TransformComponent &transform);
};
