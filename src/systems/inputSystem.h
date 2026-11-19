#pragma once
// Input system for handling keyboard/mouse input and mapping to actions.

#include "SDL3/SDL_scancode.h"
#include "foundation/ecs/systemManager.h"
#include "systems/stateSystem.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <unordered_map>

enum class Action { MoveForward, MoveBackward, MoveLeft, MoveRight, MoveUp, MoveDown };

// Values match SDL_BUTTON_LEFT/MIDDLE/RIGHT
enum class MouseButton { Left = 1, Middle = 2, Right = 3 };

class InputSystem : public BaseSystem {
public:
  InputSystem();

  void update(bool *running, SystemManager &systemManager);

  // Held down now
  bool isKeyDown(SDL_Scancode key) const;
  // Went down / came up during this frame (once per press: keyboard auto-repeat does not count)
  bool wasKeyPressed(SDL_Scancode key) const;
  bool wasKeyReleased(SDL_Scancode key) const;
  bool isActionPressed(Action action) const;

  // Mouse, for the game: clicks the game UI or the editor handled are left out
  bool isMouseButtonDown(MouseButton button) const;
  bool wasMouseButtonPressed(MouseButton button) const;
  bool wasMouseButtonReleased(MouseButton button) const;
  // Cursor position in window pixels (top-left origin); wheel movement this frame (positive is away from the user)
  glm::vec2 getMousePosition() const { return m_mousePosition; }
  float getMouseWheel() const { return m_mouseWheel; }

  float getMouseXOffset() const;
  float getMouseYOffset() const;

  void setKeyBind(Action action, SDL_Scancode keyCode);
  void bindToggleKey(SDL_Scancode key, Toggle toggle);

private:
  bool m_keys[SDL_SCANCODE_COUNT]{false};
  bool m_pressed[SDL_SCANCODE_COUNT]{false};  // this frame
  bool m_released[SDL_SCANCODE_COUNT]{false}; // this frame

  static constexpr int kMouseButtons = 6; // indexed by SDL button number (1-5)
  bool m_mouseDown[kMouseButtons]{false};
  bool m_mousePressed[kMouseButtons]{false};
  bool m_mouseReleased[kMouseButtons]{false};
  glm::vec2 m_mousePosition{0.0f};
  float m_mouseWheel = 0.0f;

  float m_mouseXOffset = 0.0f;
  float m_mouseYOffset = 0.0f;

  std::unordered_map<Action, SDL_Scancode> m_keyBinds;
  void setDefaultKeyBinds();
  void setDefaultToggleKeyBinds();

  std::unordered_map<SDL_Scancode, Toggle> m_toggleKeyBinds;
  std::unordered_map<SDL_Scancode, bool> m_prevKeyState;

  void processToggleKeys(SystemManager &systemManager);
};
