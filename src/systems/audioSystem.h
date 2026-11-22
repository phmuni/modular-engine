#pragma once
// Audio system: plays AudioSource components, one-shot effects and streamed music through miniaudio.
//
// The listener follows the active camera. Effects (sources and one-shots) pause with the game (the pause menu, the
// editor's freeze) and follow the time scale. Music keeps playing through a pause, unless setMusicPausesWithGame is on
// (then it pauses where it is, and resumes); the time scale never touches it. Without an audio device every call does
// nothing.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class AudioSystem : public BaseSystem {
public:
  enum class Bus { Effects, Music };

  explicit AudioSystem(ComponentManager &cm);
  ~AudioSystem() override;

  // Whether the next AudioSystem opens miniaudio's null backend (a device that plays to nowhere, at real speed) rather
  // than the sound card: opening a real device takes ~70 ms, and deterministic runs have nobody to listen. Sounds load,
  // play and end as usual. Before the system is created.
  static void setNullBackend(bool enabled) { s_nullBackend = enabled; }

  bool isAvailable() const;

  // Syncs listener, sources and pause state; frees finished one-shots and sounds of removed sources. Once per frame.
  // `paused`: the game is paused (the pause menu or the editor's freeze). Effects also pause at a time scale of 0.
  void update(SystemManager &systemManager, bool paused, float timeScale);

  // Decodes a sound in the background so its first play does not wait for the file
  void preload(const std::string &path);
  // Fire-and-forget effect; with a position it is placed in the world, without one it plays as is. Returns a handle
  // for stopVoice and friends (0 when nothing plays); handles are never reused, so one that ended is just not found.
  uint32_t playOneShot(const std::string &path, std::optional<glm::vec3> position, float volume, float pitch);
  void stopVoice(uint32_t voice);
  void setVoiceVolume(uint32_t voice, float volume);
  bool isVoicePlaying(uint32_t voice) const;
  // Streams from disk, replacing the current music. Returns a handle (0 when nothing plays) for the calls below, so
  // one that was replaced by newer music cannot stop that; a call with handle 0 means the music playing now.
  uint32_t playMusic(const std::string &path, float volume, bool loop);
  void stopMusic(uint32_t music = 0);
  // Volume of the music (playMusic's own volume sets it at the start)
  void setMusicVolume(float volume, uint32_t music = 0);
  // False once music that does not loop has ended, and for a handle that is no longer the current music
  bool isMusicPlaying(uint32_t music = 0) const;

  // Whether music pauses together with the game (off by default: music plays through a pause). Takes effect at the
  // next update; music started while paused then waits for the game to resume.
  void setMusicPausesWithGame(bool enabled) { m_musicPausesWithGame = enabled; }
  bool musicPausesWithGame() const { return m_musicPausesWithGame; }

  // Read-only state, for tests: whether a bus pulls audio from its sounds (false while paused), and where a sound is in
  // its file, in PCM frames (0 for a handle that is not found; voice 0 or music 0 means the music playing now)
  bool isBusRunning(Bus bus) const;
  uint64_t voiceCursor(uint32_t voice) const;
  uint64_t musicCursor(uint32_t music = 0) const;

  void setMasterVolume(float volume);
  float getMasterVolume() const;
  void setBusVolume(Bus bus, float volume);
  float getBusVolume(Bus bus) const;
  size_t getVoiceCount() const;

private:
  static inline bool s_nullBackend = false;

  struct Impl;
  std::unique_ptr<Impl> m_impl;
  ComponentManager &m_componentManager;
  bool m_musicPausesWithGame = false;
  bool isCurrentMusic(uint32_t music) const;
};
