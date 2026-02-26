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
  Entity m_selectedEntity = -1;

  UISystem(SDL_Window *window, SDL_GLContext glContext);

  void beginFrame();
  void render(EntityManager &entityManager, SystemManager &systemManager, ComponentManager &componentManager);
  void endFrame();

private:
  /// Callback used by SDL_ShowOpenFileDialog – copies the chosen path into the
  /// char buffer pointed to by @p userdata.
  static void fileDialogCallback(void *userdata, const char *const *filelist, int filter);

  /// Shows a "..." button that opens a native file dialog and writes the
  /// result into @p buf (of @p bufSize). @p id must be a unique ImGui id
  /// string. @p filters / @p nfilters are optional SDL_DialogFileFilter arrays.
  void pickFileButton(const char *id, char *buf, int bufSize,
                      SDL_Window *window,
                      const SDL_DialogFileFilter *filters = nullptr,
                      int nfilters = 0);
};
