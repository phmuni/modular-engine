# MyGame

An empty game on [modular-engine](https://github.com/phmuni/modular-engine), written in C++.

```bash
cmake -B build && cmake --build build -j     # Release; add -DCMAKE_BUILD_TYPE=Debug to debug
./bin/MyGame
```

- `src/main.cpp` is the game, with `setup`, `onSceneLoaded` and `update` ready to fill in. New `.cpp` files in `src/`
  are built automatically.
- `assets/scenes/level.json` is the level: a camera, a light, a sky and a floor. In the game, `F2` opens the editor
  (click to select, `W` `E` `R` `T` to move, rotate, scale and resize, Settings to save the scene).
- `assets/ui/pauseMenu.rml` and `pauseMenu.rcss` are the engine's pause menu (Resume, Options, Quit), there to edit. It is
  off until `setup` calls `engine.ui().pauseMenu(true)` (a line is there to uncomment): then `Escape` opens it.
- `assets/input.json` maps keys, mouse buttons and gamepad buttons and sticks to the game's actions and axes.
- `bin/` is where it builds, with `bin/engine-assets` (the engine's shaders, fonts and basic models). To ship the
  game, copy `bin/` and `assets/` together.

`--screenshot`, `--benchmark`, `--deterministic` and `--save-scene` work on the command line.
