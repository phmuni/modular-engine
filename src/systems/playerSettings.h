#pragma once
// The player's options (volumes, window size, render scale, antialiasing, ambient occlusion, fullscreen, vsync) as one
// service: a registry of named settings, each with a type, a range or a list of choices, and a getter and a setter onto
// the engine. Anything that shows or changes an option (the pause menu's document, a menu a game builds, a script) goes
// through here, so there is one place that knows what an option is and where it is saved.
//
//   engine.settings().set("msaa", 4.0);        // applies it, then keeps it in the save data
//   engine.settings().step("resolution", 1);   // the next window size
//   engine.settings().label("msaa");           // "MSAA 4x"
//
// Lua's `settings` table is this class (see systems/scriptSystem.cpp), reached from C++ as engine.settings().
//
// Saved choices are applied when the game starts, whether or not any menu is used (Engine::run, before the game's
// setup), and again when the game names itself (Engine::setGameName). The order of precedence: the engine's defaults,
// then the game's (project.json's "window" and "graphics", or what the code configured before run), then the player's
// saved choice. The values are kept under the same "engine.*" keys they have always had, so old saves still work.
//
// Adding an option is one entry in the registry in playerSettings.cpp.

#include "foundation/ecs/systemManager.h"
#include <memory>
#include <string>
#include <variant>
#include <vector>

class Engine;

class PlayerSettings : public BaseSystem {
public:
  // A number, a boolean, or a string (the choice of a list can be a number, like the MSAA samples, or a string)
  using Value = std::variant<bool, double, std::string>;
  enum class Kind { Number, Bool, Choice };
  struct Range {
    double min = 0.0;
    double max = 1.0;
    double step = 0.0;
  };

  explicit PlayerSettings(Engine &engine);
  ~PlayerSettings() override;

  // Every setting's name, in the order menus show them
  std::vector<std::string> names() const;
  bool has(const std::string &name) const;
  Kind kind(const std::string &name) const;
  // Min, max and step of a number (zeros for the others)
  Range range(const std::string &name) const;

  // What the engine uses now: a number (rounded to its step), a boolean, or for a list the choice's value. A name that
  // is not a setting gives false.
  Value get(const std::string &name);
  // Applies it, then keeps what took effect in the save data (written when the menu closes, or the game quits). A
  // number is clamped and rounded to its step; a boolean also takes a number (0 is false) or "true"/"false"; a choice
  // takes one of its values (a number snaps down to the nearest value) or its display text. False for an unknown
  // name or a value that does not fit.
  bool set(const std::string &name, const Value &value);
  // One step up (+1) or down (-1): a number by its step (stops at the ends), a choice to the next one (around the
  // ends, skipping what the engine refuses, like MSAA levels the GPU lacks), a boolean flips. Saved like set.
  bool step(const std::string &name, int direction);

  // The display texts of the choices of a list, in order (empty for a number or a boolean)
  std::vector<std::string> choices(const std::string &name);
  // What to show for the current value: "MSAA 4x", "1920 x 1080", "75%", "On"
  std::string label(const std::string &name);
  // Whether it matters now: false for what has no effect in the current state (FSR's sharpness while FSR is off or
  // the scene is drawn at full size, the window's size in fullscreen)
  bool enabled(const std::string &name);

  // Back to the game's default (what the engine had before the saved choices were applied), and out of the save data
  void reset(const std::string &name);
  void resetAll();

  // For the engine: remembers the current values as the defaults (once), and applies what the save data holds. The
  // window's size is not applied here: Engine::startWindow restores it with the window's place.
  void captureDefaults();
  void applySaved();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
