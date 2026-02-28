#pragma once
#include "foundation/ecs/systemManager.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_video.h>

// Forward declarations
class EntityManager;
class SystemManager;
class ComponentManager;
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
  void pickFileButton(const char *id, char *buf, int bufSize,
                      SDL_Window *window,
                      const SDL_DialogFileFilter *filters = nullptr,
                      int nfilters = 0);

  
};
