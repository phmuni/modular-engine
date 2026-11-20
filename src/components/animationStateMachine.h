#pragma once
// Drives an entity's Animator from a state machine: states that play a clip (or a blend of clips by a parameter),
// parameters the game sets, and transitions between states that cross-fade. The machine itself is a JSON file (see
// systems/animationGraph.h and the README); this component holds which file, the parameter values and where the
// machine is. The AnimationSystem runs it each frame, before it poses the skeleton.

#include <map>
#include <string>

struct AnimationStateMachine {
  std::string path; // the definition, relative to assets/ ("animators/character.json")

  // Parameters by name. A bool is 0 or 1, a trigger is 1 until a transition that waits for it fires. Saved in scenes;
  // a name the file does not declare is dropped when the machine starts.
  std::map<std::string, float> params;

  // Runtime, not saved: the state it is in ("" until it starts, which enters the file's default), the seconds of clip
  // time spent in it (what a transition's exitTime is measured against), and whether Engine::playAnimation took the
  // Animator over (the machine goes on running but stops driving it until it changes state)
  std::string state;
  float stateTime = 0.0f;
  bool manual = false;
};
