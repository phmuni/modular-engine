#pragma once

#include "foundation/ecs/systemManager.h"
#include <unordered_map>

enum class Toggle {
  CameraMovement,
  CameraFly,
  CursorLock,
  Wireframe,
  ShowUI,
  Shadows,
};

class StateSystem : public BaseSystem {
public:
  StateSystem();

  bool isToggled(Toggle toggle) const;
  void setToggle(Toggle toggle, bool value);
  void flipToggle(Toggle toggle);

private:
  std::unordered_map<Toggle, bool> m_states;
};
