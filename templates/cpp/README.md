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
- The player is made with `engine.create("Player", desc)` (an `EntityDesc`: model, color, `character`, game
  `components` by name, tags...), and the camera trails it with `engine.camera().follow(player, rig)`: the engine moves
  it after the physics and `update`, so it does not jitter. A picked-up coin shrinks away with
  `engine.tween().to(coin, TweenProperty::Scale, ...)`; C++ has no `wait`, so tweens' completion callbacks and
  `engine.time().after(seconds, fn)` do the sequencing.
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
- The game starts on a main menu (Play, Options, Quit), a round lasts thirty seconds, and a game over screen (Try
  again, Main menu) ends it: all eight coins is a win, the clock running out is not. The screens are documents in
  `assets/ui` with a `.rcss` for their style: `mainMenu.rml`, `gameOver.rml`, `options.rml` and `loading.rml`, yours
  to edit (the title is in `mainMenu.rml`). `main.cpp` shows them with `engine.ui().menu("ui/mainMenu.rml", options)`,
  and a button with `action('play')` calls the function given to `engine.ui().onMenuAction(...)` (`onMenuAction` in
  `MyGame`). Opening a menu pauses the game under it; `open('ui/options.rml')` in a document shows another one over it
  (the Options screen is shared by the main menu and the pause menu). `engine.ui().loadingScreen()` turns on
  `loading.rml`, which shows when a `engine.loadScene` takes more than a moment (the README of the engine, under Menu
  documents and screens, has the options and the data models).
- `Escape` opens the pause menu while playing (`engine.ui().pauseMenu(true)` in `setup`): Resume, Options and Quit. It
  is the file `assets/ui/pauseMenu.rml` (with `pauseMenu.rcss`), yours to edit; a button with `action('name')` calls
  `engine.ui().onMenuAction(...)` too, with the menu `"pause"`, and `engine.settings()` has the player's options.
- `assets/input.json` maps keys, mouse buttons and gamepad buttons and sticks to the game's actions and axes
  ("jump", "moveX"...): change it to rebind without a rebuild.
- `bin/` is where it builds, with `bin/engine-assets` (the engine's shaders, fonts and basic models). To ship the
  game, copy `bin/` and `assets/` together.

In the game, `F2` opens the editor: click a model to select it, `W` `E` `R` `T` move, rotate, scale and resize it, and
the Settings panel saves the scene. `--screenshot`, `--benchmark`, `--deterministic` and `--save-scene` work on the
command line.
