# MyGame

A game on [modular-engine](https://github.com/phmuni/modular-engine), in C++ and Lua.

```bash
cmake -B build && cmake --build build -j     # Release; add -DCMAKE_BUILD_TYPE=Debug to debug
./bin/MyGame
```

- `src/` is the C++ side (the player, here), built automatically from every `.cpp` in it. It hands functions to the
  scripts through `engine.scripts().lua()` (with sol2): `celebrate()` in `src/main.cpp`. Both sides use the same API:
  the entity handle in C++ (`player.move(...)`, `player.setEmission(...)`) is the `Entity` scripts get, and paths are
  relative to `assets/` in both.
- The player is made in C++ with `engine.create("Player", desc)` (an `EntityDesc`: model, color, `character`, game
  `components` by name...), and the camera trails it with `engine.camera().follow(player, rig)`, the same as Lua's
  `camera.follow`: the engine moves it after the physics, so it does not jitter. `coin.lua` shrinks a picked-up coin
  away with `tween.to(...)`, which C++ has too (`engine.tween()`).
- `assets/scripts/` is the Lua side (the coins and the score). Scripts reload when saved, without a rebuild:
  `game.lua` for the whole game, and an entity's Script component for that entity (`onStart`, `onUpdate`,
  `onContact`, `onOverlap`).
- `assets/prefabs/coin.json` is the coin: a prefab, an entity saved to a file, that `game.lua` copies into the level
  with `engine.spawn("prefabs/coin.json", position)`. To change every coin, run the game, press `F2`, click a coin,
  edit it in the Properties panel (color, size, its script's `spin` and `bob`...) and press **Save as prefab**
  (set the path to `../assets/prefabs/coin.json` first: it suggests the entity's name). The next coins use it.
- The game starts on a main menu (Play, Options, Quit), a round lasts thirty seconds, and a game over screen (Try
  again, Main menu) ends it. `game.lua` has the flow (`ui.menu`, `onMenuAction`), and the screens are documents in
  `assets/ui` with a `.rcss` for their style: `mainMenu.rml`, `gameOver.rml`, `options.rml` and `loading.rml`, yours
  to edit. The loading screen (`ui.loadingScreen()`) shows when an `engine.loadScene` takes more than a moment; the
  engine's README, under Menu documents and screens, has the options.
- `Escape` opens the pause menu while playing (`ui.pauseMenu(true)` in `game.lua`; its document is
  `assets/ui/pauseMenu.rml`, yours to edit, and a button's `action('name')` calls `onMenuAction("pause", name)` in
  `game.lua`). `game.lua` keeps the best score between runs with `save.set` / `save.get` and shows it on the HUD
  (`assets/ui/hud.rml`) with `ui.text`.
- `assets/scenes/level.json` is the level, its sky color (the `Sky` entity) included: `F2` in the game opens the
  editor, `Ctrl+P` plays and stops.
- `assets/input.json` maps keys and gamepad buttons to the actions and axes.
- `bin/` is where it builds. To ship the game, copy `bin/` and `assets/` together.

Install the **Lua** extension (`sumneko.lua`; VS Code offers it when you open the folder) for autocomplete and type
hints on the engine's API in your scripts: `.luarc.json` and `types/engine.lua` set it up.
