#pragma once
// Drives an entity's Animator from a state machine: states that play a clip (or a blend of clips by a parameter),
// parameters the game sets, and transitions between states that cross-fade. The machine itself is a JSON file (see
// systems/animationGraph.h and the README); this component holds which file, the parameter values and where the
// machine is. The AnimationSystem runs it each frame, before it poses the skeleton.

#include "components/animator.h"
#include <map>
#include <string>
#include <vector>

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
  // Runtime, not saved: whether the state's clock has moved since it was entered. Its first move counts the clip's very
  // start as crossed, so an animation event at 0 fires; see AnimationSystem and the README, "Animation events".
  bool clockMoved = false;

  // Runtime, not saved: where each of the file's layers is, in the file's order (matched to the file by name)
  struct LayerState {
    std::string name;
    std::string state;
    float stateTime = 0.0f;
    // What the layer plays: the clip, its blend, and the crossfade out of the one before. It stays on the clip it
    // played while the layer fades out to an empty state.
    Animator playback;
    std::string reference; // the clip an additive layer measures its clip against, when its state names one
    // How much of the layer shows, 0 to 1: it rises while the layer enters a state that plays something and falls
    // while it enters an empty one, over the transition's fade
    float presence = 0.0f;
    float presenceTarget = 0.0f;
    float presenceRate = 0.0f;  // per second
    float effectiveWeight = 0.0f; // the layer's weight times its presence, as of the last frame
    bool clockMoved = false;      // as for the base's, above
  };
  std::vector<LayerState> layers;
};
