#pragma once
// State system for managing global toggles like wireframe mode, cursor lock, and camera movement.

#include "foundation/core/toggle.h"
#include "foundation/ecs/systemManager.h"

class StateSystem : public BaseSystem {
public:
  StateSystem();

  bool isToggled(Toggle toggle) const;
  void setToggle(Toggle toggle, bool value);
  void flipToggle(Toggle toggle);

private:
  std::unordered_map<Toggle, bool> m_states;
};
