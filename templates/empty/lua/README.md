# MyGame

An empty game on [modular-engine](https://github.com/phmuni/modular-engine), written in Lua: nothing to compile.

```bash
./bin/MyGame
```

- `assets/scenes/level.json` is the level: a camera, a light, a sky and a floor. In the game, `F2` opens the editor
  (click to select, `W` `E` `R` `T` to move, rotate, scale and resize, the Properties panel to edit and to give an
  entity a script, Settings to save). `Ctrl+P` plays and stops.
- `assets/scripts/game.lua` is the game script, with `onStart` and `onUpdate` ready to fill in. Scripts reload when saved.
  An entity's script (its Script component in the editor) gets `onStart(self)`, `onUpdate(self, dt)`,
  `onContact(self, other, point, normal)` and `onOverlap(self, other)`.
- `assets/ui/pauseMenu.rml` and `pauseMenu.rcss` are the engine's pause menu (Resume, Options, Quit), there to edit. It is
  off until `game.lua` calls `ui.pauseMenu(true)`: then `Escape` opens it.
- `assets/input.json` maps keys and gamepad buttons to the actions and axes the scripts read.
- `project.json` says where the game starts: its scene, game script and input file, and optionally its window and graphics.
- `bin/` is the engine's Lua runner, named after the game. To ship the game, copy the whole folder.

Install the **Lua** extension (`sumneko.lua`; VS Code offers it when you open the folder) for autocomplete and type
hints on the engine's API in your scripts: `.luarc.json` and `types/engine.lua` set it up.
