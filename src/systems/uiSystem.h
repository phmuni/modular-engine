#pragma once
// ImGui-based editor UI: scene explorer, properties panel, and entity creation.

#include "foundation/ecs/systemManager.h"
#include <SDL3/SDL.h>

class EntityManager;
class SystemManager;
class ComponentManager;
class Engine;
class SceneSystem;
class ResourceSystem;
class RenderSystem;
using Entity = int;

class UISystem : public BaseSystem {
public:
  Entity selectedEntity = -1;
  bool m_renderUI = false;

  UISystem(SDL_Window *window, SDL_GLContext glContext);

  void beginFrame();
  void update(SystemManager &systemManager);
  void render(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
              ComponentManager &componentManager);
  void endFrame();

private:
  static void fileDialogCallback(void *userdata, const char *const *filelist, int filter);
  void pickFileButton(const char *id, char *buf, int bufSize, SDL_Window *window,
                      const SDL_DialogFileFilter *filters = nullptr, int nfilters = 0);

  bool renderCameraInspector(Entity entity, SystemManager &systemManager, ComponentManager &componentManager,
                             ResourceSystem &resourceSystem, SDL_Window *window);
  bool renderParticleInspector(Entity entity, ComponentManager &componentManager);
  bool renderMaterialInspector(Entity entity, ComponentManager &componentManager, ResourceSystem &resourceSystem,
                               RenderSystem &renderSystem, SDL_Window *window);
  void renderAddEntityPopup(Engine &engine, SceneSystem &sceneSystem, SystemManager &systemManager,
                            ComponentManager &componentManager, ResourceSystem &resourceSystem,
                            RenderSystem &renderSystem, SDL_Window *window);
};
