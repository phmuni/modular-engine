#pragma once
// A Lua script that gives the entity its behavior (see ScriptSystem). The file defines any of:
//
//   function onStart(self) end                         -- once, before its first update
//   function onUpdate(self, dt) end                    -- every frame
//   function onContact(self, other, point, normal) end -- a physics body started touching it
//   function onOverlap(self, other) end                -- its Collision box overlaps another (every frame)
//
// `self` is the entity; fields set on it (self.speed = 5) are its own state, and survive the file being edited and
// reloaded. Paths are relative to the assets folder: "scripts/coin.lua".

#include <string>

struct Script {
  std::string path;

  Script() = default;
  explicit Script(std::string scriptPath) : path(std::move(scriptPath)) {}
};
