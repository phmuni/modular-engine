#pragma once
// SDL event polling, key binds, and toggle key processing.

#include "SDL3/SDL_scancode.h"
#include "foundation/ecs/systemManager.h"
#include "systems/stateSystem.h"
#include <SDL3/SDL.h>
#include <unordered_map>

enum class Action { MoveForward, MoveBackward, MoveLeft, MoveRight, MoveUp, MoveDown };

class InputSystem : public BaseSystem {
public:
  InputSystem();

  void update(bool *running, SystemManager &systemManager);

  bool isKeyPressed(SDL_Scancode key) const;
  bool isActionPressed(Action action) const;

  float getMouseXOffset() const;
  float getMouseYOffset() const;

  void setKeyBind(Action action, SDL_Scancode keyCode);
  void bindToggleKey(SDL_Scancode key, Toggle toggle);

private:
  bool m_keys[SDL_SCANCODE_COUNT]{false};

  float m_mouseXOffset = 0.0f;
  float m_mouseYOffset = 0.0f;

  std::unordered_map<Action, SDL_Scancode> m_keyBinds;
  void setDefaultKeyBinds();
  void setDefaultToggleKeyBinds();

  std::unordered_map<SDL_Scancode, Toggle> m_toggleKeyBinds;
  std::unordered_map<SDL_Scancode, bool> m_prevKeyState;

  void processToggleKeys(SystemManager &systemManager);
};
