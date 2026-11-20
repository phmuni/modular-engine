# MyGame

A game on [modular-engine](https://github.com/phmuni/modular-engine).

```bash
cmake -B build && cmake --build build -j     # Release; add -DCMAKE_BUILD_TYPE=Debug to debug
./bin/MyGame
```

- `src/` is the game's code: `src/main.cpp` to start with. New `.cpp` files there are built automatically.
- `src/main.cpp` is the same game as the Lua project template, written against the same API (entity handles,
  `engine.ui()`, `engine.camera()`, `engine.spawn(...)`), so what you learn here carries over to Lua scripts and back.
  It says in comments where C++ does it its own way (components and loops instead of `self` and `onUpdate`).
- `assets/` holds its files, referenced relative to it: `"prefabs/coin.json"`, `"ui/hud.rml"`. The level is
  `assets/scenes/level.json`, and its sky color is the `Sky` entity in it (`engine.setSkyColor(...)` changes it from code).
- `assets/prefabs/coin.json` is the coin: a prefab, an entity saved to a file, that `src/main.cpp` copies into the level
  with `engine.spawn("prefabs/coin.json", position)`. Its `Coin` component is registered in `main`, so the file keeps
  its fields. To change every coin, run the game, press `F2`, click a coin, edit it in the Properties panel and press
  **Save as prefab** (set the path to `../assets/prefabs/coin.json` first: it suggests the entity's name). The next
  coins use it.
- `assets/ui/hud.rml` is the score on screen (RmlUi: HTML-like markup and CSS), changed from code with
  `engine.ui().text("score", n)`. The best score is kept between runs with `engine.saveData()`, in a folder named by
  `engine.setGameName`.
- `Escape` opens the pause menu (`engine.ui().pauseMenu(true)` in `setup`): Resume, Options and Quit.
- `assets/input.json` maps keys, mouse buttons and gamepad buttons and sticks to the game's actions and axes
  ("jump", "moveX"...): change it to rebind without a rebuild.
- `bin/` is where it builds, with `bin/engine-assets` (the engine's shaders, fonts and basic models). To ship the
  game, copy `bin/` and `assets/` together.

In the game, `F2` opens the editor: click a model to select it, `W` `E` `R` `T` move, rotate, scale and resize it, and
the Settings panel saves the scene. `--screenshot`, `--benchmark`, `--deterministic` and `--save-scene` work on the
command line.
