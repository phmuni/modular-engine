# MyGame

A game on [modular-engine](https://github.com/phmuni/modular-engine).

```bash
cmake -B build && cmake --build build -j     # Release; add -DCMAKE_BUILD_TYPE=Debug to debug
./bin/MyGame
```

- `src/` is the game's code: `src/main.cpp` to start with. New `.cpp` files there are built automatically.
- `assets/` holds its files, referenced as `"../assets/..."`. The level is `assets/scenes/level.json`.
- `assets/input.json` maps keys, mouse buttons and gamepad buttons and sticks to the game's actions and axes
  ("jump", "moveX"...): change it to rebind without a rebuild.
- `bin/` is where it builds, with `bin/engine-assets` (the engine's shaders, fonts and basic models). To ship the
  game, copy `bin/` and `assets/` together.

In the game, `F2` opens the editor: click a model to select it, `W` `E` `R` `T` move, rotate, scale and resize it, and
the Settings panel saves the scene. `--screenshot`, `--benchmark`, `--deterministic` and `--save-scene` work on the
command line.
