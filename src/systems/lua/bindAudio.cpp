// Lua bindings: the sound table (sounds and music).
// A binding only converts types and calls the C++ game API: see the rule at the top of luaBindings.h.

#include <sol/sol.hpp> // SOL_ALL_SAFETIES_ON comes from the build, the same for every file that includes sol2

#include "systems/lua/luaBindings.h"

#include "components/animator.h"
#include "components/camera.h"
#include "components/characterController.h"
#include "components/hidden.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/rigidBody.h"
#include "components/script.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/gameEntity.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/navigationSystem.h"
#include "systems/physicsSystem.h"
#include "systems/playerSettings.h"
#include "systems/renderSystem.h"
#include "systems/saveData.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <optional>
#include <tuple>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

void LuaBindings::bindAudio() {
  // sound: play and playAt return a handle (0: nothing played) for stop, setVolume and isPlaying
  sol::table sound = lua.create_named_table("sound");
  sound.set_function("play", [this](const std::string &path, sol::optional<float> volume, sol::optional<float> pitch) {
    return engine.playSound(path, volume.value_or(1.0f), pitch.value_or(1.0f));
  });
  sound.set_function("playAt", [this](const std::string &path, const glm::vec3 &position, sol::optional<float> volume) {
    return engine.playSoundAt(path, position, volume.value_or(1.0f));
  });
  sound.set_function("stop", [this](uint32_t handle) { engine.stopSound(handle); });
  sound.set_function("setVolume", [this](uint32_t handle, float volume) { engine.setSoundVolume(handle, volume); });
  sound.set_function("isPlaying", [this](uint32_t handle) { return engine.isSoundPlaying(handle); });
  sound.set_function("preload", [this](const std::string &path) { engine.preloadSound(path); });
  // music returns a handle like play does (0: nothing plays); the others take it to leave newer music alone, or
  // none for the music playing now
  sound.set_function("music", [this](const std::string &path, sol::optional<float> volume, sol::optional<bool> loop) {
    return engine.playMusic(path, volume.value_or(1.0f), loop.value_or(true));
  });
  sound.set_function("stopMusic", [this](sol::optional<uint32_t> handle) { engine.stopMusic(handle.value_or(0)); });
  sound.set_function("setMusicVolume", [this](float volume, sol::optional<uint32_t> handle) {
    engine.setMusicVolume(volume, handle.value_or(0));
  });
  sound.set_function("isMusicPlaying", [this](sol::optional<uint32_t> handle) {
    return engine.isMusicPlaying(handle.value_or(0));
  });
}
