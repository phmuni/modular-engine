#pragma once
// A Lua script that gives the entity its behavior (see ScriptSystem). The file defines any of:
//
//   properties = { speed = 6, color = vec3(1, 0.8, 0.2), label = "coin", enabled = true } -- tunable values
//   function onStart(self) end                         -- once, before its first update
//   function onUpdate(self, dt) end                    -- every frame
//   function onContact(self, other, point, normal) end -- a physics body started touching it
//   function onContactEnd(self, other) end             -- a physics body that touched it stopped touching
//   function onOverlap(self, other) end                -- its Collision box overlaps another (every frame)
//
// `self` is the entity; fields set on it (self.speed = 5) are its own state, and survive the file being edited and
// reloaded. Each property is a field of `self` too, starting at the script's value or at this entity's own (see
// Script::properties). Paths are relative to the assets folder: "scripts/coin.lua".

#include <glm/glm.hpp>
#include <map>
#include <string>
#include <variant>

// The kinds of value a script property can have: a boolean, a number, a string or a vec3
using ScriptValue = std::variant<bool, float, std::string, glm::vec3>;

struct Script {
  std::string path;
  // This entity's values for the script's properties, by name; the ones it does not list keep the script's own.
  // Names the script no longer declares stay here (and in the scene file) until the script declares them again.
  std::map<std::string, ScriptValue> properties;

  Script() = default;
  explicit Script(std::string scriptPath) : path(std::move(scriptPath)) {}
};
