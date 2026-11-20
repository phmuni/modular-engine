#pragma once
// Plays a clip of the entity's animated model (glTF animations). Engine::playAnimation switches clips with a
// crossfade; the AnimationSystem advances `time` and poses the skeleton each frame.

#include <string>

struct Animator {
  std::string clip;   // the clip playing, by name; empty: the model's first
  float time = 0.0f;  // seconds into it
  float speed = 1.0f; // 2: twice as fast, negative: backward
  bool loop = true;
  bool playing = true; // a clip that does not loop stops (playing = false) at its end

  // A blend of two clips (set by the AnimationStateMachine for a state that blends by a parameter): `clip` and
  // `blendClip` mixed by `blendWeight` (0: only `clip`, 1: only `blendClip`). While blendClip is set, `time` is how far
  // along the cycle it is, 0 to 1, so the two clips stay in step whatever their lengths. Not saved.
  std::string blendClip;
  float blendWeight = 0.0f;

  // Crossfade (set by Engine::playAnimation): the clip it is fading out, where that one was, and how far along
  std::string fadeFrom;
  float fadeFromTime = 0.0f;
  float fadeElapsed = 0.0f;
  float fadeDuration = 0.0f;
};
