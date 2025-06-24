#pragma once

#include "ecs/entityManager.h"

class ActiveCameraManager {
private:
  Entity activeCamera = -1;

public:
  void setActiveCamera(Entity cameraEntity);
  Entity getActiveCamera() const;
  bool hasActiveCamera() const;
};
