// Engine: raycasts, picking and collision queries.

#include "foundation/core/engine.h"

#include "components/animationStateMachine.h"
#include "components/collision.h"
#include "components/hidden.h"
#include "components/camera.h"
#include "components/cameraRig.h"
#include "components/model.h"
#include "components/name.h"
#include "components/parent.h"
#include "components/tags.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "foundation/core/random.h"
#include "foundation/core/typeName.h"
#include <cstdint>
#include "rendering/resources/material.h"
#include "rendering/resources/textureProcessing.h"
#include "systems/animationGraph.h"
#include "systems/animationSystem.h"
#include "systems/scriptSystem.h"
#include "systems/audioSystem.h"
#include "systems/cameraSystem.h"
#include "systems/collisionSystem.h"
#include "systems/gameUISystem.h"
#include "systems/inputSystem.h"
#include "systems/lightSystem.h"
#include "systems/modelQueries.h"
#include "systems/navigationSystem.h"
#include "systems/particleSystem.h"
#include "systems/loadingScreen.h"
#include "systems/menu.h"
#include "systems/pauseMenu.h"
#include "systems/playerSettings.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/saveData.h"
#include "systems/sceneSerializer.h"
#include "systems/sceneSystem.h"
#include "systems/timeSystem.h"
#include "systems/transformSystem.h"
#include "systems/editorUISystem.h"
#include "components/particleEmitter.h"
#include "systems/windowSystem.h"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <utility>
#include <filesystem>
#include <limits>

std::optional<RayHit> Engine::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                      const std::function<bool(Entity)> &accept, uint32_t layerMask) {
  return m_systemManager.getSystem<CollisionSystem>().raycast(origin, direction, maxDistance, m_componentManager,
                                                             accept, layerMask);
}

std::optional<RayHit> Engine::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                      const QueryOptions &options) {
  return raycast(origin, direction, maxDistance, [&options](Entity entity) { return options.accepts(entity); },
                 options.layerMask);
}

std::optional<Engine::Ray> Engine::screenRay(glm::vec2 windowPosition) {
  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? tryGet<Camera>(cameraEntity) : nullptr;
  int width = 0, height = 0;
  SDL_GetWindowSize(m_systemManager.getSystem<WindowSystem>().getWindow(), &width, &height);
  if (!camera || width <= 0 || height <= 0)
    return std::nullopt;

  // Window to normalized device coordinates (y up), then back through the camera onto the near and far planes
  const glm::vec2 ndc(windowPosition.x / width * 2.0f - 1.0f, 1.0f - windowPosition.y / height * 2.0f);
  const glm::mat4 inverse =
      glm::inverse(cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera));
  glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f);
  glm::vec4 farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
  nearPoint /= nearPoint.w;
  farPoint /= farPoint.w;
  return Ray{glm::vec3(nearPoint), glm::normalize(glm::vec3(farPoint - nearPoint))};
}

std::optional<RayHit> Engine::raycastModels(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                            const std::function<bool(Entity)> &accept) {
  const ModelQueries queries(m_componentManager, m_systemManager.getSystem<ResourceSystem>(),
                             m_systemManager.getSystem<TransformSystem>());
  if (auto hit = queries.raycast(origin, direction, maxDistance, accept))
    return RayHit{hit->entity, hit->distance, hit->point, hit->normal};
  return std::nullopt;
}

std::optional<RayHit> Engine::pickModel(glm::vec2 windowPosition) {
  if (auto ray = screenRay(windowPosition))
    return raycastModels(ray->origin, ray->direction);
  return std::nullopt;
}

void Engine::fitCollisionToModel(Entity entity, float fit, bool isStatic) {
  const Model *model = tryGet<Model>(entity);
  const Transform *transform = tryGet<Transform>(entity);
  if (!model || !transform) {
    LOG_W("[Engine] fitCollisionToModel: entity %d needs a Model and a Transform", entity);
    return;
  }
  const Mesh &mesh = m_systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle);
  Transform unmoved = *transform;
  unmoved.position = glm::vec3(0.0f); // the box is relative to the Transform's position
  const glm::mat4 matrix = m_systemManager.getSystem<TransformSystem>().calculateModelMatrix(unmoved);

  // The eight corners of the model's box, rotated and scaled, then boxed again along the world axes
  const glm::vec3 low = mesh.getBoundsMin(), high = mesh.getBoundsMax();
  glm::vec3 boxMin(std::numeric_limits<float>::max()), boxMax(-std::numeric_limits<float>::max());
  for (int corner = 0; corner < 8; ++corner) {
    const glm::vec3 local(corner & 1 ? high.x : low.x, corner & 2 ? high.y : low.y, corner & 4 ? high.z : low.z);
    const glm::vec3 world = glm::vec3(matrix * glm::vec4(local, 1.0f));
    boxMin = glm::min(boxMin, world);
    boxMax = glm::max(boxMax, world);
  }
  const glm::vec3 center = (boxMin + boxMax) * 0.5f, half = (boxMax - boxMin) * 0.5f * fit;

  Collision &collision = has<Collision>(entity) ? get<Collision>(entity) : addComponent<Collision>(entity);
  collision.min = center - half;
  collision.max = center + half;
  collision.isStatic = isStatic;
}

const std::vector<CollisionEvent> &Engine::findCollisions() {
  return m_systemManager.getSystem<CollisionSystem>().findCollisions(m_componentManager);
}

bool Engine::isColliding(Entity a, Entity b) {
  return m_systemManager.getSystem<CollisionSystem>().overlaps(a, b, m_componentManager);
}
