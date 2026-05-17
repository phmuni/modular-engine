#pragma once
// SDL3 window and OpenGL context initialization.

#include "foundation/ecs/systemManager.h"
#include "rendering/renderer.h"

class WindowSystem : public BaseSystem {
private:
  SDL_Window *m_window;
  SDL_GLContext m_glContext;

public:
  WindowSystem(float screenWidth, float screenHeight);
  bool initialize(float screenWidth, float screenHeight);
  void onResize(int newWidth, int newHeight, Renderer &renderer);
  SDL_Window *getWindow() const;
  SDL_GLContext getContext() const;
  void setCursor(bool boolean);
};
