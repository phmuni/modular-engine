# MyGame

A game on [modular-engine](https://github.com/phmuni/modular-engine), in C++ and Lua.

```bash
cmake -B build && cmake --build build -j     # Release; add -DCMAKE_BUILD_TYPE=Debug to debug
./bin/MyGame
```

- `src/` is the C++ side (the player, here), built automatically from every `.cpp` in it. It hands functions to the
  scripts through `engine.scripts().lua()` (with sol2): `celebrate()` in `src/main.cpp`.
- `assets/scripts/` is the Lua side (the coins and the score). Scripts reload when saved, without a rebuild:
  `game.lua` for the whole game, and an entity's Script component for that entity (`onStart`, `onUpdate`,
  `onContact`, `onOverlap`).
- `Escape` opens the pause menu (`engine.enablePauseMenu(true)` in `src/main.cpp`), and `game.lua` keeps the best
  score between runs with `save.set` / `save.get`.
- `assets/scenes/level.json` is the level: `F2` in the game opens the editor, `Ctrl+P` plays and stops.
- `assets/input.json` maps keys and gamepad buttons to the actions and axes.
- `bin/` is where it builds. To ship the game, copy `bin/` and `assets/` together.

Install the **Lua** extension (`sumneko.lua`; VS Code offers it when you open the folder) for autocomplete and type
hints on the engine's API in your scripts: `.luarc.json` and `types/engine.lua` set it up.
