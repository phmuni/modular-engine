#include "manager/cameraManager.h"

void ActiveCameraManager::setActiveCamera(Entity cameraEntity) { activeCamera = cameraEntity; }

Entity ActiveCameraManager::getActiveCamera() const { return activeCamera; }

bool ActiveCameraManager::hasActiveCamera() const { return activeCamera != -1; }
