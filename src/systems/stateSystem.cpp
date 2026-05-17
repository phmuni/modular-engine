
// State system implementation for managing global states and toggles that affect various aspects of the engine's
// behavior.

#include "systems/stateSystem.h"
#include "foundation/core/config.h"

StateSystem::StateSystem() {
  for (int i = 0; i < EngineConfig::DEFAULT_TOGGLES_COUNT; ++i) {
    m_states[EngineConfig::DEFAULT_TOGGLES[i].first] = EngineConfig::DEFAULT_TOGGLES[i].second;
  }
}

bool StateSystem::isToggled(Toggle toggle) const {
  auto it = m_states.find(toggle);
  return it != m_states.end() && it->second;
}

void StateSystem::setToggle(Toggle toggle, bool value) { m_states[toggle] = value; }

void StateSystem::flipToggle(Toggle toggle) { m_states[toggle] = !isToggled(toggle); }
