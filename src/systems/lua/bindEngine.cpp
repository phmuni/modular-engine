// Lua bindings: print, wait and the engine table (entities, scenes, window, graphics, sky).
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

void LuaBindings::bindEngine() {
  // print goes to the engine's log
  lua.set_function("print", [](sol::variadic_args args, sol::this_state state) {
    sol::state_view view(state);
    std::string line;
    for (auto arg : args) {
      if (!line.empty())
        line += "  ";
      line += view["tostring"](arg.get<sol::object>()).get<std::string>();
    }
    LOG_I("[Lua] %s", line.c_str());
  });

  // wait(seconds) suspends the coroutine it is called in (see engine.run) for that much game time; wait() until the
  // next frame
  lua.set_function("wait", sol::yielding([](sol::optional<float> seconds, sol::this_state state) {
    if (!lua_isyieldable(state.L))
      throw std::runtime_error("wait() only works inside a coroutine: engine.run(function() ... end) or "
                               "self:run(function() ... end)");
    return seconds.value_or(0.0f);
  }));

  // engine: entities, scenes, the sky
  sol::table api = lua.create_named_table("engine");
  // engine.run(function() ... wait(1) ... end) runs a function that can wait; returns a handle for engine.stop. It
  // lasts across scene loads; use self:run for one that should end with an entity
  api.set_function("run", [this](sol::protected_function function) { return startRoutine(function, -1, "engine.run"); });
  api.set_function("stop", [this](int handle) { stopRoutine(handle); });
  api.set_function("find", [this](const std::string &name) { return entityObject(engine.find(name)); });
  // The entities named so, or with a component ("Model", "Script", a game component's name), by entity number
  api.set_function("findAll", [this](const std::string &name) { return sol::as_table(engine.findAll(name)); });
  // The entities with a tag (self:addTag("enemy"), or tags = {"enemy"} in engine.create), by entity number
  api.set_function("findTagged", [this](const std::string &tag) { return sol::as_table(engine.findTagged(tag)); });
  api.set_function("findWithComponent",
                   [this](const std::string &component) { return sol::as_table(engine.findWithComponent(component)); });
  api.set_function("spawn", [this](const std::string &prefab, sol::optional<glm::vec3> position,
                                   sol::optional<glm::vec3> rotation) {
    return entityObject(engine.spawn(prefab, position.value_or(glm::vec3(0.0f)), rotation.value_or(glm::vec3(0.0f))));
  });
  // engine.create("Coin", { model = "models/coin.glb", position = vec3(0, 1, 0), scale = vec3(0.5),
  //                         color = vec3(1, 0.8, 0.2), body = "dynamic", shape = "sphere", script = "scripts/coin.lua" })
  // engine.create(name, { model = "box" | "sphere" | "cylinder" | path, position, rotation, scale, color, emission, glow,
  //                       body = "static" | "dynamic" | "kinematic", shape, mass, collision = true, script,
  //                       tags = {"enemy"}, character = { radius = 0.4, height = 1.8 }, components = {"Player"} })
  api.set_function("create", [this](const std::string &name, sol::optional<sol::table> options) {
    return entityObject(engine.create(name, entityDesc(options)));
  });
  api.set_function("destroy", [](sol::optional<GameEntity> e) {
    if (e)
      e->destroy();
  });
  api.set_function("loadScene", [this](const std::string &path) { engine.loadScene(path); });
  // The file last loaded, "" before one was; engine.isLoadingScene() is true while a loadScene waits for its models
  api.set_function("scenePath", [this]() { return engine.getScenePath(); });
  // Several scenes at once: loadSceneAdditive adds a file to the open ones (waits for its models like loadScene, does
  // nothing for one already open), unloadScene removes one (false if it is not open or the only one); loadedScenes lists
  // the files in the order they were opened, activeScene is the one new entities and scenePath mean
  api.set_function("loadSceneAdditive", [this](const std::string &path) { engine.loadSceneAdditive(path); });
  api.set_function("unloadScene", [this](const std::string &path) { return engine.unloadScene(path); });
  api.set_function("loadedScenes", [this]() { return sol::as_table(engine.loadedScenes()); });
  api.set_function("activeScene", [this]() { return engine.getActiveScene(); });
  api.set_function("setActiveScene", [this](const std::string &path) { return engine.setActiveScene(path); });
  api.set_function("isLoadingScene", [this]() { return engine.isLoadingScene(); });
  api.set_function("sceneLoadProgress", [this]() { return engine.sceneLoadProgress(); });
  // Reads a model on a worker thread so creating it later does not stall a frame
  api.set_function("preload", [this](const std::string &path) { engine.preloadModel(path); });
  // engine.screenshot("shot.bmp", 0, false) saves a BMP of the next frame (after `delay` seconds; quitAfter closes the game)
  api.set_function("screenshot", [this](const std::string &path, sol::optional<float> delay, sol::optional<bool> quitAfter) {
    engine.captureScreenshot(path, delay.value_or(0.0f), quitAfter.value_or(false));
  });
  // Picking, with window coordinates such as input.mouse() gives: engine.screenRay(input.mouse()) returns the active
  // camera's ray through that point as origin, direction (nil without a camera); engine.pick(input.mouse()) the
  // nearest model it hits, as { entity, point, normal, distance }, or nil
  api.set_function("screenRay", [this](float x, float y) {
    sol::variadic_results results;
    if (const auto ray = engine.screenRay(glm::vec2(x, y))) {
      results.push_back(sol::make_object(lua, ray->origin));
      results.push_back(sol::make_object(lua, ray->direction));
    } else {
      results.push_back(sol::make_object(lua, sol::lua_nil));
    }
    return results;
  });
  api.set_function("pick", [this](float x, float y) -> sol::object {
    const auto hit = engine.pickModel(glm::vec2(x, y));
    return hit ? sol::object(hitTable(*hit)) : sol::object(sol::lua_nil);
  });
  api.set_function("setTitle", [this](const std::string &title) { engine.setWindowTitle(title); });
  api.set_function("quit", [this]() { engine.quit(); });
  // The window and the resolution the scene renders at (see Engine::setWindowSize, Engine::setRenderScale).
  // engine.windowSize() and engine.renderResolution() return width and height; engine.resolutions() a list of
  // { width = 1280, height = 720 } that fit the display.
  api.set_function("setWindowSize", [this](int width, int height) { engine.setWindowSize(width, height); });
  api.set_function("windowSize", [this]() {
    const glm::ivec2 size = engine.windowSize();
    return std::make_tuple(size.x, size.y);
  });
  api.set_function("resolutions", [this]() {
    sol::table result = lua.create_table();
    for (const glm::ivec2 &size : engine.resolutions()) {
      sol::table entry = lua.create_table();
      entry["width"] = size.x;
      entry["height"] = size.y;
      result.add(entry);
    }
    return result;
  });
  api.set_function("setRenderScale", [this](float scale) { engine.setRenderScale(scale); });
  api.set_function("renderScale", [this]() { return engine.renderScale(); });
  api.set_function("renderResolution", [this]() {
    const glm::ivec2 size = engine.renderResolution();
    return std::make_tuple(size.x, size.y);
  });
  // Antialiasing and upscaling (see Engine::setMsaa and friends)
  api.set_function("setMsaa", [this](int samples) { engine.setMsaa(samples); });
  api.set_function("msaa", [this]() { return engine.msaa(); });
  api.set_function("setFxaa", [this](bool enabled) { engine.setFxaa(enabled); });
  api.set_function("fxaa", [this]() { return engine.fxaa(); });
  api.set_function("setFsr", [this](bool enabled) { engine.setFsr(enabled); });
  api.set_function("fsr", [this]() { return engine.fsr(); });
  api.set_function("setFsrSharpness", [this](float stops) { engine.setFsrSharpness(stops); });
  api.set_function("fsrSharpness", [this]() { return engine.fsrSharpness(); });
  api.set_function("setFullscreen", [this](bool enabled) { engine.setFullscreen(enabled); });
  api.set_function("isFullscreen", [this]() { return engine.isFullscreen(); });
  // Ambient occlusion (see Engine::setSsao): engine.setSsao(false), engine.ssao(); engine.setSsaoSettings{ radius = 0.5,
  // intensity = 1, quality = "medium", halfResolution = true } changes only the fields given (quality: "low", "medium"
  // or "high"); engine.ssaoSettings() returns them all
  api.set_function("setSsao", [this](bool enabled) { engine.setSsao(enabled); });
  api.set_function("ssao", [this]() { return engine.ssao(); });
  api.set_function("setSsaoSettings", [this](const sol::table &options) {
    SsaoSettings settings = engine.ssaoSettings();
    settings.radius = options.get_or("radius", settings.radius);
    settings.intensity = options.get_or("intensity", settings.intensity);
    settings.halfResolution = options.get_or("halfResolution", settings.halfResolution);
    if (const sol::optional<std::string> quality = options["quality"]) {
      if (const auto parsed = ssaoQualityFromName(*quality))
        settings.quality = *parsed;
      else
        LOG_W("[Script] engine.setSsaoSettings: quality is \"low\", \"medium\" or \"high\", not \"%s\"",
              quality->c_str());
    }
    engine.setSsaoSettings(settings);
  });
  api.set_function("ssaoSettings", [this]() {
    const SsaoSettings settings = engine.ssaoSettings();
    sol::table result = lua.create_table();
    result["radius"] = settings.radius;
    result["intensity"] = settings.intensity;
    result["quality"] = ssaoQualityName(settings.quality);
    result["halfResolution"] = settings.halfResolution;
    return result;
  });
  // The engine's switches by name (see TOGGLE_NAMES): engine.setToggle("freeCamera", true)
  const auto toggleNamed = [](const std::string &name) {
    if (const auto toggle = toggleFromName(name))
      return *toggle;
    std::string known;
    for (const auto &entry : TOGGLE_NAMES)
      known += (known.empty() ? "" : ", ") + std::string(entry.first);
    throw std::runtime_error("engine.setToggle: \"" + name + "\" is not a toggle (" + known + ")");
  };
  api.set_function("setToggle", [this, toggleNamed](const std::string &name, bool on) {
    engine.setState(toggleNamed(name), on);
  });
  api.set_function("isToggled", [this, toggleNamed](const std::string &name) {
    return engine.getState(toggleNamed(name));
  });
  // The color the image is cleared to, behind the skybox panorama; kept in the scene (see Engine::setSkyColor)
  api.set_function("setSkyColor", [this](const glm::vec3 &color) { engine.setSkyColor(color); });
  api.set_function("skyColor", [this]() { return engine.skyColor(); });
  // Fog: engine.setFog({ color = vec3(0.6, 0.7, 0.8), start = 20, finish = 90 }) turns it on, with only the fields given
  // changed (mode = "linear" or "exponential", density for the exponential one; without `color` it takes the sky color);
  // engine.setFog(false) turns it off. engine.fog() reads it back.
  api.set_function("setFog", [this](sol::object settings) {
    Fog fog = engine.fog();
    if (settings.is<bool>()) {
      fog.enabled = settings.as<bool>();
    } else if (settings.is<sol::table>()) {
      const sol::table o = settings.as<sol::table>();
      fog.enabled = o.get_or("enabled", true);
      if (const sol::optional<glm::vec3> color = o["color"]) {
        fog.color = *color;
        fog.matchSky = false;
      }
      fog.matchSky = o.get_or("matchSky", fog.matchSky);
      if (const sol::optional<std::string> mode = o["mode"]) {
        if (*mode != "linear" && *mode != "exponential")
          throw std::runtime_error("engine.setFog: mode is \"linear\" or \"exponential\"");
        fog.mode = *mode == "exponential" ? FogMode::Exponential : FogMode::Linear;
      }
      fog.start = o.get_or("start", fog.start);
      fog.finish = o.get_or("finish", fog.finish);
      fog.density = o.get_or("density", fog.density);
    } else {
      throw std::runtime_error("engine.setFog: a table of settings, or false");
    }
    engine.setFog(fog);
  });
  api.set_function("fog", [this]() {
    const Fog fog = engine.fog();
    sol::table result = lua.create_table();
    result["enabled"] = fog.enabled;
    result["mode"] = fog.mode == FogMode::Exponential ? "exponential" : "linear";
    result["matchSky"] = fog.matchSky;
    result["color"] = fog.matchSky ? engine.skyColor() : fog.color;
    result["start"] = fog.start;
    result["finish"] = fog.finish;
    result["density"] = fog.density;
    return result;
  });
  // Light added to everything lit, on top of each Light's own ambient: engine.setAmbient(vec3(0.5, 0.6, 0.8), 0.3)
  // (intensity 1 without one; engine.setAmbient(vec3(1), 0) is off). engine.ambient() returns the color and intensity.
  api.set_function("setAmbient", [this](const glm::vec3 &color, sol::optional<float> intensity) {
    engine.setAmbient(color, intensity.value_or(1.0f));
  });
  api.set_function("ambient", [this]() { return std::make_tuple(engine.ambientColor(), engine.ambientIntensity()); });
}
