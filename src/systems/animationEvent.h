#pragma once
// What an animation state machine tells the game while a clip plays: a moment of it passing (a footstep, a swoosh) or a
// window of it opening and closing (the part of a swing that hurts). A state declares them in the machine's file (see
// the README, "Animation events"); Engine::onAnimationEvent and the entity script's onAnimationEvent hear of them.

#include <string>

struct AnimationEvent {
  // Fire: a point event's moment came. Begin and End: a window opened or closed; every Begin has its End, also when the
  // state is left, the machine goes away or the layer stops showing while the window is open.
  enum class Phase { Fire, Begin, End };

  std::string name;
  Phase phase = Phase::Fire;
  std::string layer; // "base" for the top-level states
  std::string state; // the state the event belongs to
};
