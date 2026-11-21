// Audio system implementation on top of miniaudio's high-level engine (mixing runs on miniaudio's own thread).

#include "systems/audioSystem.h"

#include "components/audioSource.h"
#include "components/camera.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "systems/cameraSystem.h"
#include <algorithm>
#include <filesystem>
#include <miniaudio.h>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr ma_uint32 kEffectFlags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC;
constexpr ma_uint32 kMusicFlags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_NO_SPATIALIZATION;
} // namespace

struct AudioSystem::Impl {
  struct Voice {
    std::unique_ptr<ma_sound> sound;
    Entity owner = -1;      // -1 for one-shots, which are freed when they end
    std::string sourcePath; // AudioSource::path as written, to notice when it changes
    bool wantPlaying = false;
    uint64_t seenFrame = 0;
  };

  ma_context context{}; // only for the null backend: the engine makes its own otherwise
  bool hasContext = false;
  ma_engine engine{};
  ma_sound_group effects{};
  ma_sound_group music{};
  bool ready = false;

  std::unordered_map<uint32_t, Voice> voices;
  uint32_t nextVoice = 1;
  uint64_t frame = 0;

  // One decoded copy per effect, kept so replays share it instead of decoding the file again
  std::unordered_map<std::string, std::unique_ptr<ma_sound>> decoded;
  std::unordered_set<std::string> missing;
  std::unique_ptr<ma_sound> currentMusic;
  uint32_t musicHandle = 0; // of currentMusic, from the same counter as the voices

  float masterVolume = 1.0f;
  float effectsVolume = 1.0f;
  float musicVolume = 1.0f;
  bool paused = false;

  ~Impl() {
    if (!ready) {
      if (hasContext)
        ma_context_uninit(&context);
      return;
    }
    // Sounds before their groups, groups before the engine
    for (auto &[id, voice] : voices)
      destroySound(voice.sound);
    for (auto &[path, sound] : decoded)
      destroySound(sound);
    destroySound(currentMusic);
    ma_sound_group_uninit(&effects);
    ma_sound_group_uninit(&music);
    ma_engine_uninit(&engine);
    if (hasContext)
      ma_context_uninit(&context);
  }

  static void destroySound(std::unique_ptr<ma_sound> &sound) {
    if (sound) {
      ma_sound_uninit(sound.get());
      sound.reset();
    }
  }

  // Checked up front: an asynchronous load would otherwise fail silently later
  bool exists(const std::string &path) {
    if (std::filesystem::exists(path))
      return true;
    if (missing.insert(path).second)
      LOG_W("[AudioSystem] Sound not found: %s", path.c_str());
    return false;
  }

  std::unique_ptr<ma_sound> createSound(const std::string &path, ma_uint32 flags, ma_sound_group *group) {
    if (!ready || !exists(path))
      return nullptr;
    auto sound = std::make_unique<ma_sound>();
    ma_result result = ma_sound_init_from_file(&engine, path.c_str(), flags, group, nullptr, sound.get());
    if (result != MA_SUCCESS) {
      LOG_W("[AudioSystem] Could not load %s: %s", path.c_str(), ma_result_description(result));
      return nullptr;
    }
    return sound;
  }

  void ensureDecoded(const std::string &path) {
    if (decoded.count(path))
      return;
    if (auto sound = createSound(path, kEffectFlags, &effects))
      decoded.emplace(path, std::move(sound));
  }

  // An effect voice: shares the decoded data of `path`
  std::unique_ptr<ma_sound> createEffect(const std::string &path) {
    ensureDecoded(path);
    return decoded.count(path) ? createSound(path, kEffectFlags, &effects) : nullptr;
  }

  void freeVoice(Voice &voice) { destroySound(voice.sound); }
};

AudioSystem::AudioSystem(ComponentManager &cm) : m_impl(std::make_unique<Impl>()), m_componentManager(cm) {
  ma_engine_config config = ma_engine_config_init();
  if (s_nullBackend) {
    const ma_backend backends[] = {ma_backend_null};
    if (ma_context_init(backends, 1, nullptr, &m_impl->context) == MA_SUCCESS) {
      m_impl->hasContext = true;
      config.pContext = &m_impl->context;
    }
  }
  ma_result result = ma_engine_init(&config, &m_impl->engine);
  if (result != MA_SUCCESS) {
    LOG_W("[AudioSystem] No audio device, sound disabled: %s", ma_result_description(result));
    return;
  }
  ma_sound_group_init(&m_impl->engine, 0, nullptr, &m_impl->effects);
  ma_sound_group_init(&m_impl->engine, 0, nullptr, &m_impl->music);
  m_impl->ready = true;
  LOG_I("[AudioSystem] Audio device ready: %u Hz, %u channels", ma_engine_get_sample_rate(&m_impl->engine),
        ma_engine_get_channels(&m_impl->engine));
}

AudioSystem::~AudioSystem() = default;

bool AudioSystem::isAvailable() const { return m_impl->ready; }

void AudioSystem::update(SystemManager &systemManager, bool paused, float timeScale) {
  auto &impl = *m_impl;
  if (!impl.ready)
    return;
  ++impl.frame;

  // A stopped group stops pulling audio from its sounds, which pauses them where they are
  paused = paused || timeScale <= 0.0f;
  if (paused != impl.paused) {
    impl.paused = paused;
    if (paused)
      ma_sound_group_stop(&impl.effects);
    else
      ma_sound_group_start(&impl.effects);
  }
  ma_sound_group_set_pitch(&impl.effects, std::max(timeScale, 0.01f));

  // Listener at the active camera
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  Entity cameraEntity = cameraSystem.getActiveCamera();
  if (auto *camera = cameraEntity != -1 ? m_componentManager.tryGet<Camera>(cameraEntity) : nullptr) {
    glm::vec3 position = camera->position;
    if (camera->isRelative)
      if (auto *transform = m_componentManager.tryGet<Transform>(cameraEntity))
        position += transform->position;
    ma_engine_listener_set_position(&impl.engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&impl.engine, 0, camera->front.x, camera->front.y, camera->front.z);
    ma_engine_listener_set_world_up(&impl.engine, 0, camera->up.x, camera->up.y, camera->up.z);
  }

  // Sources
  m_componentManager.forEachComponent<AudioSource>([&](Entity entity, AudioSource &source) {
    auto it = impl.voices.find(source.voice);
    if (it != impl.voices.end() && (it->second.owner != entity || it->second.sourcePath != source.path))
      it = impl.voices.end(); // copied from another entity, or given a new file: the old voice is freed below

    if (it == impl.voices.end()) {
      source.voice = 0;
      source.rewind = false; // a new voice starts at the beginning anyway
      if (!source.playing || source.path.empty())
        return;
      auto sound = impl.createEffect(EngineConfig::resolveAsset(source.path));
      if (!sound) {
        source.playing = false;
        return;
      }
      uint32_t id = impl.nextVoice++;
      it = impl.voices.emplace(id, Impl::Voice{std::move(sound), entity, source.path}).first;
      source.voice = id;
    }

    Impl::Voice &voice = it->second;
    voice.seenFrame = impl.frame;
    ma_sound *sound = voice.sound.get();

    ma_sound_set_volume(sound, source.volume);
    ma_sound_set_pitch(sound, std::max(source.pitch, 0.01f));
    ma_sound_set_looping(sound, source.loop);

    auto *transform = m_componentManager.tryGet<Transform>(entity);
    bool spatial = source.spatial && transform;
    ma_sound_set_spatialization_enabled(sound, spatial);
    if (spatial) {
      ma_sound_set_position(sound, transform->position.x, transform->position.y, transform->position.z);
      ma_sound_set_min_distance(sound, source.minDistance);
      ma_sound_set_max_distance(sound, source.maxDistance);
      ma_sound_set_rolloff(sound, source.rolloff);
    }

    if (source.rewind) {
      ma_sound_seek_to_pcm_frame(sound, 0);
      source.rewind = false;
    }
    const bool wantPlaying = source.playing && !source.paused;
    if (wantPlaying && !voice.wantPlaying) {
      ma_sound_start(sound); // from the start again if it had ended
      voice.wantPlaying = true;
    } else if (!wantPlaying && voice.wantPlaying) {
      ma_sound_stop(sound);
      voice.wantPlaying = false;
    } else if (voice.wantPlaying && ma_sound_at_end(sound)) {
      source.playing = false;
      voice.wantPlaying = false;
    }
  });

  // Finished one-shots, and voices whose source was removed (or whose entity was destroyed)
  for (auto it = impl.voices.begin(); it != impl.voices.end();) {
    Impl::Voice &voice = it->second;
    bool done = voice.owner == -1 ? ma_sound_at_end(voice.sound.get()) : voice.seenFrame != impl.frame;
    if (done) {
      impl.freeVoice(voice);
      it = impl.voices.erase(it);
    } else {
      ++it;
    }
  }
}

void AudioSystem::preload(const std::string &path) {
  if (m_impl->ready)
    m_impl->ensureDecoded(EngineConfig::resolveAsset(path));
}

uint32_t AudioSystem::playOneShot(const std::string &path, std::optional<glm::vec3> position, float volume,
                                  float pitch) {
  auto &impl = *m_impl;
  auto sound = impl.ready ? impl.createEffect(EngineConfig::resolveAsset(path)) : nullptr;
  if (!sound)
    return 0;

  ma_sound_set_volume(sound.get(), volume);
  ma_sound_set_pitch(sound.get(), std::max(pitch, 0.01f));
  ma_sound_set_spatialization_enabled(sound.get(), position.has_value());
  if (position) {
    const AudioSource defaults;
    ma_sound_set_position(sound.get(), position->x, position->y, position->z);
    ma_sound_set_min_distance(sound.get(), defaults.minDistance);
    ma_sound_set_max_distance(sound.get(), defaults.maxDistance);
    ma_sound_set_rolloff(sound.get(), defaults.rolloff);
  }
  ma_sound_start(sound.get());

  const uint32_t id = impl.nextVoice++;
  impl.voices.emplace(id, Impl::Voice{std::move(sound), -1, {}, true, impl.frame});
  return id;
}

void AudioSystem::stopVoice(uint32_t voice) {
  auto it = m_impl->voices.find(voice);
  if (it == m_impl->voices.end() || it->second.owner != -1) // a source's voice belongs to its component
    return;
  m_impl->freeVoice(it->second);
  m_impl->voices.erase(it);
}

void AudioSystem::setVoiceVolume(uint32_t voice, float volume) {
  auto it = m_impl->voices.find(voice);
  if (it != m_impl->voices.end() && it->second.owner == -1)
    ma_sound_set_volume(it->second.sound.get(), std::max(volume, 0.0f));
}

bool AudioSystem::isVoicePlaying(uint32_t voice) const {
  auto it = m_impl->voices.find(voice);
  return it != m_impl->voices.end() && it->second.owner == -1 && !ma_sound_at_end(it->second.sound.get());
}

uint32_t AudioSystem::playMusic(const std::string &path, float volume, bool loop) {
  auto &impl = *m_impl;
  if (!impl.ready)
    return 0;
  Impl::destroySound(impl.currentMusic);
  impl.musicHandle = 0;
  impl.currentMusic = impl.createSound(EngineConfig::resolveAsset(path), kMusicFlags, &impl.music);
  if (!impl.currentMusic)
    return 0;
  ma_sound_set_volume(impl.currentMusic.get(), volume);
  ma_sound_set_looping(impl.currentMusic.get(), loop);
  ma_sound_start(impl.currentMusic.get());
  impl.musicHandle = impl.nextVoice++;
  return impl.musicHandle;
}

// 0 is whatever plays; a handle of music that was replaced matches nothing
bool AudioSystem::isCurrentMusic(uint32_t music) const {
  return m_impl->currentMusic && (music == 0 || music == m_impl->musicHandle);
}

void AudioSystem::stopMusic(uint32_t music) {
  if (isCurrentMusic(music)) {
    Impl::destroySound(m_impl->currentMusic);
    m_impl->musicHandle = 0;
  }
}

void AudioSystem::setMusicVolume(float volume, uint32_t music) {
  if (isCurrentMusic(music))
    ma_sound_set_volume(m_impl->currentMusic.get(), std::max(volume, 0.0f));
}

bool AudioSystem::isMusicPlaying(uint32_t music) const {
  return isCurrentMusic(music) && !ma_sound_at_end(m_impl->currentMusic.get());
}

void AudioSystem::setMasterVolume(float volume) {
  m_impl->masterVolume = std::max(volume, 0.0f);
  if (m_impl->ready)
    ma_engine_set_volume(&m_impl->engine, m_impl->masterVolume);
}

float AudioSystem::getMasterVolume() const { return m_impl->masterVolume; }

void AudioSystem::setBusVolume(Bus bus, float volume) {
  volume = std::max(volume, 0.0f);
  float &stored = bus == Bus::Music ? m_impl->musicVolume : m_impl->effectsVolume;
  stored = volume;
  if (m_impl->ready)
    ma_sound_group_set_volume(bus == Bus::Music ? &m_impl->music : &m_impl->effects, volume);
}

float AudioSystem::getBusVolume(Bus bus) const {
  return bus == Bus::Music ? m_impl->musicVolume : m_impl->effectsVolume;
}

size_t AudioSystem::getVoiceCount() const { return m_impl->voices.size(); }
