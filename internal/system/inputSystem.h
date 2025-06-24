#pragma once

#include <SDL3/SDL.h>
#include <unordered_map>

enum class Action { MoveForward, MoveBackward, MoveLeft, MoveRight, MoveUp, MoveDown };

class InputSystem {
private:
  float mouseXOffset = 0.0f;
  float mouseYOffset = 0.0f;
  bool quitRequested = false;

  bool keys[SDL_SCANCODE_COUNT]{false};

  std::unordered_map<Action, SDL_Scancode> keyBinds;

  void setDefaultKeyBinds();

public:
  InputSystem();

  bool update(bool *running);
  bool isActionPressed(Action action) const;

  void setKeyBind(Action action, SDL_Scancode keyCode);

  // Getters
  float getMouseXOffset() const;
  float getMouseYOffset() const;
  bool isQuitRequested() const;
  bool isKeyPressed(SDL_Scancode key) const;

  // Setters
  void setMouseXOffset(float mouseXOffset);
  void setMouseYOffset(float mouseYOffset);
  void setQuitRequested(bool quitRequested);
  void setKeyPressed(SDL_Scancode key, bool pressed);
};
