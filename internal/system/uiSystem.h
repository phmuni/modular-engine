#pragma once

#include "SDL3/SDL.h"                 // IWYU pragma: keep
#include "component/nameComponent.h"  // IWYU pragma: keep
#include "ecs/systemManager.h"        // IWYU pragma: keep
#include "imgui/imgui.h"              // IWYU pragma: keep
#include "imgui/imgui_impl_opengl3.h" // IWYU pragma: keep
#include "imgui/imgui_impl_sdl3.h"    // IWYU pragma: keep
#include "system/renderSystem.h"      // IWYU pragma: keep


class UISystem {
public:
  UISystem(SDL_Window *w, SDL_GLContext gl);

  void beginFrame();
  void render(SystemManager &systemManager, ComponentManager &componentManager);
  void endFrame();
};
