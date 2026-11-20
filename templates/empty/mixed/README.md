# MyGame

An empty game on [modular-engine](https://github.com/phmuni/modular-engine), in C++ and Lua.

```bash
cmake -B build && cmake --build build -j     # Release; add -DCMAKE_BUILD_TYPE=Debug to debug
./bin/MyGame
```

- `src/main.cpp` is the C++ side (`setup`, `onSceneLoaded`, `update`), built from every `.cpp` in `src/`. It can hand
  functions to the scripts through `engine.scripts().lua()` (an example is commented there).
- `assets/scripts/game.lua` is the Lua side, with `onStart` and `onUpdate` ready to fill in. Scripts reload when
  saved, without a rebuild. An entity's script (its Script component in the editor) gets `onStart(self)`,
  `onUpdate(self, dt)`, `onContact` and `onOverlap`.
- `assets/scenes/level.json` is the level: a camera, a light, a sky and a floor. In the game, `F2` opens the editor,
  `Ctrl+P` plays and stops.
- `assets/ui/pauseMenu.rml` and `pauseMenu.rcss` are the engine's pause menu (Resume, Options, Quit), there to edit. It is
  off until `game.lua` calls `ui.pauseMenu(true)` (a line is there to uncomment): then `Escape` opens it.
- `assets/input.json` maps keys and gamepad buttons to the actions and axes.
- `bin/` is where it builds. To ship the game, copy `bin/` and `assets/` together.

Install the **Lua** extension (`sumneko.lua`; VS Code offers it when you open the folder) for autocomplete and type
hints on the engine's API in your scripts: `.luarc.json` and `types/engine.lua` set it up.
