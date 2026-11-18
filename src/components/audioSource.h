#pragma once
// Audio source component: a sound attached to an entity, positioned at its Transform when spatial.

#include <cstdint>
#include <string>

struct AudioSource {
  std::string path;   // WAV, MP3 or FLAC; relative paths resolve from the executable's folder like models do
  float volume = 1.0f;
  float pitch = 1.0f;
  bool loop = true;
  bool spatial = true; // follows the entity's Transform and fades with distance from the camera
  bool playing = true; // set to start or stop it; a sound that does not loop sets it back to false when it ends

  // Distance fade (spatial only): full volume up to minDistance, quieter beyond it, no further change past maxDistance.
  // Positioned one-shots (Engine::playSoundAt) use these defaults too.
  float minDistance = 10.0f;
  float maxDistance = 100.0f;
  float rolloff = 1.0f;

  uint32_t voice = 0; // owned by AudioSystem
};
