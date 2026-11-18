#pragma once
// Audio system: plays AudioSource components, one-shot effects and streamed music through miniaudio.
//
// The listener follows the active camera. Effects (sources and one-shots) pause with Toggle::Paused and follow the
// time scale; music keeps playing. Without an audio device every call does nothing.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>

class AudioSystem : public BaseSystem {
public:
  enum class Bus { Effects, Music };

  explicit AudioSystem(ComponentManager &cm);
  ~AudioSystem() override;

  bool isAvailable() const;

  // Syncs listener, sources and pause state; frees finished one-shots and sounds of removed sources. Once per frame.
  void update(SystemManager &systemManager, bool paused, float timeScale);

  // Decodes a sound in the background so its first play does not wait for the file
  void preload(const std::string &path);
  // Fire-and-forget effect; with a position it is placed in the world, without one it plays as is
  void playOneShot(const std::string &path, std::optional<glm::vec3> position, float volume, float pitch);
  // Streams from disk, replacing the current music
  void playMusic(const std::string &path, float volume, bool loop);
  void stopMusic();

  void setMasterVolume(float volume);
  float getMasterVolume() const;
  void setBusVolume(Bus bus, float volume);
  float getBusVolume(Bus bus) const;
  size_t getVoiceCount() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  ComponentManager &m_componentManager;
};
