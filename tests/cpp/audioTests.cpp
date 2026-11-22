// Audio: effects and music against the two kinds of pause (the pause menu's Toggle::Paused, the editor's freeze) and
// the time scale. The engine runs miniaudio's null backend in tests: a device that plays at real speed to nowhere, so a
// sound's playback cursor advances while it plays and stands still while its bus is stopped.

#include "foundation/core/engine.h"
#include "systems/audioSystem.h"
#include "systems/gameTime.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <functional>
#include <memory>

namespace {
const char *kSound = "sounds/music.wav"; // about two seconds: it does not end during a test
constexpr Uint64 kSettleMs = 50;          // for the mixing thread to notice a stopped or started group
constexpr Uint64 kSpanMs = 150;           // to watch a cursor stand still (frames can be much faster than audio)
constexpr int kFrames = 100000;           // the limit of a wait on audio; a frame can take well under a millisecond

AudioSystem &audio(Engine &engine) { return engine.getSystemManager().getSystem<AudioSystem>(); }

// What a test follows: the cursor of the effect or of the music
struct Watch {
  bool music = false;
  uint32_t handle = 0;
  uint64_t mark = 0;
};

uint64_t cursor(Engine &engine, const Watch &watch) {
  return watch.music ? audio(engine).musicCursor(watch.handle) : audio(engine).voiceCursor(watch.handle);
}

// Waits `ms` of real time (the audio runs on the clock, the frames do not)
void waitMs(testing::Steps &steps, Uint64 ms) {
  auto start = std::make_shared<Uint64>(0);
  steps.step(0, [start] { *start = SDL_GetTicks(); });
  steps.until([start, ms] { return SDL_GetTicks() - *start >= ms; }, "time passes", kFrames);
}

// Adds steps that wait for the sound to play, apply `pause`, and check the cursor stands (or goes on) meanwhile, then
// that it moves again after `resume`
void checkPausing(Engine &engine, testing::Steps &steps, std::shared_ptr<Watch> watch, bool stands,
                  std::function<void()> pause, std::function<void()> resume) {
  Engine *game = &engine;
  steps.until([=] { return cursor(*game, *watch) > 0; }, "the sound plays", kFrames);
  steps.step(0, [=] { pause(); });
  waitMs(steps, kSettleMs);
  steps.step(0, [=] { watch->mark = cursor(*game, *watch); });
  waitMs(steps, kSpanMs);
  steps.step(0, [=] {
    if (stands)
      CHECK_EQ(cursor(*game, *watch), watch->mark);
    else
      CHECK(cursor(*game, *watch) > watch->mark);
    watch->mark = cursor(*game, *watch);
    resume();
  });
  // It plays again, from where it was
  steps.until([=] { return cursor(*game, *watch) > watch->mark; }, "it plays again", kFrames);
}

// While the editor has the game frozen the app does not update, so no test step runs: a timer thread watches the
// cursor and resumes, as the editor's buttons would (see PausedProbe in runtimeTests.cpp)
struct FrozenProbe {
  Engine *game = nullptr;
  Watch watch;
  std::atomic<int> stage{0}; // 1: first reading taken, 2: second taken and resumed
  uint64_t first = 0, second = 0;
  bool frozen = false, effectsRunning = true, musicRunning = true;
};

Uint32 SDLCALL readFirst(void *data, SDL_TimerID, Uint32) {
  auto *probe = static_cast<FrozenProbe *>(data);
  probe->first = cursor(*probe->game, probe->watch);
  probe->stage = 1;
  return 0;
}

Uint32 SDLCALL readSecondAndResume(void *data, SDL_TimerID, Uint32) {
  auto *probe = static_cast<FrozenProbe *>(data);
  Engine &game = *probe->game;
  probe->second = cursor(game, probe->watch);
  probe->frozen = game.isFrozen();
  probe->effectsRunning = audio(game).isBusRunning(AudioSystem::Bus::Effects);
  probe->musicRunning = audio(game).isBusRunning(AudioSystem::Bus::Music);
  game.resume();
  probe->stage = 2;
  return 0;
}

// Waits for the sound to play, freezes the game (pause()), and checks from the timer thread that the cursor stands
// still (or goes on) for 200 ms; then the game is resumed and the sound must go on
void checkFrozen(Engine &engine, testing::Steps &steps, std::shared_ptr<FrozenProbe> probe, bool stands) {
  Engine *game = &engine;
  probe->game = game;
  steps.until([=] { return cursor(*game, probe->watch) > 0; }, "the sound plays", kFrames);
  steps.step(0, [=] {
    game->pause();
    SDL_AddTimer(150, readFirst, probe.get());
    SDL_AddTimer(350, readSecondAndResume, probe.get());
  });
  steps.until([=] { return probe->stage == 2; }, "the probe resumed the game", kFrames);
  steps.step(0, [=] {
    CHECK(probe->frozen);
    CHECK(!game->isFrozen());
    CHECK(probe->first > 0);
    if (stands) {
      CHECK_EQ(probe->second, probe->first);
      CHECK(!(probe->watch.music ? probe->musicRunning : probe->effectsRunning));
    } else {
      CHECK(probe->second > probe->first);
    }
    probe->first = cursor(*game, probe->watch);
  });
  steps.until([=] { return cursor(*game, probe->watch) > probe->first; }, "it plays again", kFrames); // plays again, from where it was
}
} // namespace

TEST_CASE(audio_available) {
  CHECK(audio(engine).isAvailable());
  CHECK(engine.musicPausesWithGame()); // music pauses with the pause menu by default
  engine.setMusicPausesWithGame(false);
  CHECK(!engine.musicPausesWithGame());
}

TEST_CASE(audio_effects_pause_with_toggle) {
  auto watch = std::make_shared<Watch>();
  watch->handle = engine.playSound(kSound);
  CHECK(watch->handle != 0);
  Engine *game = &engine;
  checkPausing(engine, steps, watch, true, [game] { game->setState(Toggle::Paused, true); },
               [game] { game->setState(Toggle::Paused, false); });
  steps.step(0, [game] { CHECK(audio(*game).isBusRunning(AudioSystem::Bus::Effects)); });
}

TEST_CASE(audio_effects_pause_with_editor_pause) {
  engine.play(); // a test under way, then Pause freezes it (PlayMode::Paused)
  auto probe = std::make_shared<FrozenProbe>();
  Engine *game = &engine;
  steps.step(2, [game, probe] {
    CHECK(game->getPlayMode() == Engine::PlayMode::Testing);
    probe->watch.handle = game->playSound(kSound);
    CHECK(probe->watch.handle != 0);
  });
  checkFrozen(engine, steps, probe, true);
}

// pause() of a running game freezes it without a test (Editing)
TEST_CASE(audio_effects_pause_when_editing) {
  auto probe = std::make_shared<FrozenProbe>();
  probe->watch.handle = engine.playSound(kSound);
  checkFrozen(engine, steps, probe, true);
}

// With the option off, music plays through the pause menu
TEST_CASE(audio_music_plays_through_pause_when_option_off) {
  engine.setMusicPausesWithGame(false);
  auto watch = std::make_shared<Watch>();
  watch->music = true;
  watch->handle = engine.playMusic(kSound);
  CHECK(watch->handle != 0);
  Engine *game = &engine;
  checkPausing(engine, steps, watch, false, [game] { game->setState(Toggle::Paused, true); },
               [game] { game->setState(Toggle::Paused, false); });
  steps.step(0, [game] { CHECK(audio(*game).isBusRunning(AudioSystem::Bus::Music)); });
}

// The editor's freeze pauses music even with the option off (only the pause menu follows the option)
TEST_CASE(audio_music_pauses_with_editor_freeze_option_off) {
  engine.setMusicPausesWithGame(false);
  auto probe = std::make_shared<FrozenProbe>();
  probe->watch.music = true;
  probe->watch.handle = engine.playMusic(kSound);
  checkFrozen(engine, steps, probe, true);
}

TEST_CASE(audio_music_pauses_with_editor_pause_option_off) {
  engine.setMusicPausesWithGame(false);
  engine.play();
  auto probe = std::make_shared<FrozenProbe>();
  probe->watch.music = true;
  Engine *game = &engine;
  steps.step(2, [game, probe] { probe->watch.handle = game->playMusic(kSound); });
  checkFrozen(engine, steps, probe, true);
}

TEST_CASE(audio_music_pauses_with_toggle) {
  engine.setMusicPausesWithGame(true);
  auto watch = std::make_shared<Watch>();
  watch->music = true;
  watch->handle = engine.playMusic(kSound);
  Engine *game = &engine;
  checkPausing(engine, steps, watch, true, [game] { game->setState(Toggle::Paused, true); },
               [game] { game->setState(Toggle::Paused, false); });
  steps.step(0, [game] { CHECK(audio(*game).isBusRunning(AudioSystem::Bus::Music)); });
}

TEST_CASE(audio_music_pauses_with_editor_pause) {
  engine.setMusicPausesWithGame(true);
  engine.play();
  auto probe = std::make_shared<FrozenProbe>();
  probe->watch.music = true;
  Engine *game = &engine;
  steps.step(2, [game, probe] { probe->watch.handle = game->playMusic(kSound); });
  checkFrozen(engine, steps, probe, true);
}

TEST_CASE(audio_music_pauses_when_editing) {
  engine.setMusicPausesWithGame(true);
  auto probe = std::make_shared<FrozenProbe>();
  probe->watch.music = true;
  probe->watch.handle = engine.playMusic(kSound);
  checkFrozen(engine, steps, probe, true);
}

// Music started while the game is paused (option on) waits for the resume; replacing it keeps it paused
TEST_CASE(audio_music_started_while_paused) {
  engine.setMusicPausesWithGame(true);
  Engine *game = &engine;
  auto watch = std::make_shared<Watch>();
  watch->music = true;
  engine.setState(Toggle::Paused, true);
  waitMs(steps, kSettleMs);
  steps.step(0, [game, watch] {
    CHECK(!audio(*game).isBusRunning(AudioSystem::Bus::Music));
    watch->handle = game->playMusic(kSound);
    CHECK(watch->handle != 0);
  });
  waitMs(steps, kSpanMs);
  steps.step(0, [game, watch] {
    CHECK_EQ(cursor(*game, *watch), uint64_t(0)); // not audible: nothing was pulled
    watch->handle = game->playMusic(kSound);      // replaced while paused
  });
  waitMs(steps, kSpanMs);
  steps.step(0, [game, watch] {
    CHECK_EQ(cursor(*game, *watch), uint64_t(0));
    game->setState(Toggle::Paused, false);
  });
  steps.until([=] { return cursor(*game, *watch) > 0; }, "the music plays after the resume", kFrames);
}

TEST_CASE(audio_music_started_while_paused_option_off) {
  engine.setMusicPausesWithGame(false);
  Engine *game = &engine;
  auto watch = std::make_shared<Watch>();
  watch->music = true;
  engine.setState(Toggle::Paused, true);
  waitMs(steps, kSettleMs);
  steps.step(0, [game, watch] { watch->handle = game->playMusic(kSound); });
  steps.until([=] { return cursor(*game, *watch) > 0; }, "the music plays through the pause", kFrames);
}

// Time scale 0 stops the effects but neither pauses nor bends music, with the option on as well
TEST_CASE(audio_time_scale_zero_keeps_music) {
  engine.setMusicPausesWithGame(true);
  auto music = std::make_shared<Watch>();
  music->music = true;
  music->handle = engine.playMusic(kSound);
  auto effect = std::make_shared<Watch>();
  effect->handle = engine.playSound(kSound);
  Engine *game = &engine;
  steps.until([=] { return cursor(*game, *music) > 0 && cursor(*game, *effect) > 0; }, "both play", kFrames);
  steps.step(0, [game] { game->time().setScale(0.0f); });
  waitMs(steps, kSettleMs);
  steps.step(0, [=] {
    music->mark = cursor(*game, *music);
    effect->mark = cursor(*game, *effect);
    CHECK(audio(*game).isBusRunning(AudioSystem::Bus::Music));
    CHECK(!audio(*game).isBusRunning(AudioSystem::Bus::Effects));
  });
  waitMs(steps, kSpanMs);
  steps.step(0, [=] {
    CHECK(cursor(*game, *music) > music->mark);
    CHECK_EQ(cursor(*game, *effect), effect->mark);
    game->time().setScale(1.0f);
  });
}
