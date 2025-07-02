#pragma once

#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <glad/glad.h>

class WindowManager {
private:
  SDL_Window *window;
  SDL_GLContext glContext;

public:
  WindowManager() : window(nullptr), glContext(nullptr) {};

  bool initialize(float screenWidth, float screenHeight);

  SDL_Window *getWindow() const;
  SDL_GLContext getContext() const;
  void setCursor(bool boolean);
};