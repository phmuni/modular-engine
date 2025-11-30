#pragma once

#include "SDL3/SDL_scancode.h"
#include "ecs/systemManager.h"
#include <SDL3/SDL.h>
#include <core/renderer.h>
#include <unordered_map>

enum class Action { MoveForward, MoveBackward, MoveLeft, MoveRight, MoveUp, MoveDown, MoveMouse };

class InputSystem : public BaseSystem {
private:
  float mouseXOffset = 0.0f;
  float mouseYOffset = 0.0f;
  bool quitRequested = false;
  bool controlEnabled = true;
  bool toggleKeyLastState = false;

  bool keys[SDL_SCANCODE_COUNT]{false};

  std::unordered_map<Action, SDL_Scancode> keyBinds;

  void setDefaultKeyBinds();

public:
  InputSystem();

  bool update(bool *running, SystemManager &systemManager);
  bool isActionPressed(Action action) const;

  void setKeyBind(Action action, SDL_Scancode keyCode);

  float getMouseXOffset() const;
  float getMouseYOffset() const;
  bool getMove() const;
  bool isQuitRequested() const;
  bool isKeyPressed(SDL_Scancode key) const;

  void setMouseXOffset(float mouseXOffset);
  void setMouseYOffset(float mouseYOffset);
  void setQuitRequested(bool quitRequested);
  void setKeyPressed(SDL_Scancode key, bool pressed);
};
