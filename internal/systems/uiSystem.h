#pragma once
// ImGui-based editor UI: scene explorer, properties panel, and entity creation.

#include "foundation/ecs/systemManager.h"
#include <SDL3/SDL.h>

class EntityManager;
class SystemManager;
class ComponentManager;
class SceneSystem;
class ResourceSystem;
class RenderSystem;
using Entity = int;

class UISystem : public BaseSystem {
public:
  Entity selectedEntity = -1;

  UISystem(SDL_Window *window, SDL_GLContext glContext);

  void beginFrame();
  void render(EntityManager &entityManager, SystemManager &systemManager, ComponentManager &componentManager);
  void endFrame();

private:
  static void fileDialogCallback(void *userdata, const char *const *filelist, int filter);
  void pickFileButton(const char *id, char *buf, int bufSize, SDL_Window *window,
                      const SDL_DialogFileFilter *filters = nullptr, int nfilters = 0);

  void renderCameraInspector(Entity entity, SystemManager &systemManager, ComponentManager &componentManager,
                             ResourceSystem &resourceSystem, SDL_Window *window);
  void renderParticleInspector(Entity entity, ComponentManager &componentManager);
  void renderMaterialInspector(Entity entity, ComponentManager &componentManager, ResourceSystem &resourceSystem,
                               RenderSystem &renderSystem, SDL_Window *window);
  void renderAddEntityPopup(SceneSystem &sceneSystem, EntityManager &entityManager, ComponentManager &componentManager,
                            ResourceSystem &resourceSystem, RenderSystem &renderSystem, SDL_Window *window);
};
