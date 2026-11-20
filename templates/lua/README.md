# MyGame

A game on [modular-engine](https://github.com/phmuni/modular-engine), written in Lua: nothing to compile.

```bash
./bin/MyGame
```

- `assets/scripts/` holds the game's behavior. `game.lua` runs for the whole game; an entity's script (its Script
  component) runs for that entity: `onStart(self)`, `onUpdate(self, dt)`, `onContact(self, other, point, normal)`,
  `onOverlap(self, other)`. Save a script while the game runs and it reloads, keeping each entity's state. Its
  `properties` table (speed, jumpSpeed...) is what the Properties panel lets you tune, per entity.
- `assets/scenes/level.json` is the level, its sky color too (the `Sky` entity; `engine.setSkyColor(vec3(...))` changes it
  from a script). In the game, `F2` opens the editor: click to select, `W` `E` `R` `T` to move,
  rotate, scale and resize, the Properties panel to edit (and to give an entity a script), Settings to save.
  `Ctrl+P` freezes the game, plays it from there and puts the level back on Stop.
- `Escape` opens the pause menu (`ui.pauseMenu(true)` in `game.lua`): Resume, Options (volumes, fullscreen, vsync)
  and Quit. The best score is kept between runs with `save.set` / `save.get` (see `game.lua`).
- `assets/prefabs/coin.json` is the coin: a prefab, an entity saved to a file, that `game.lua` copies into the level
  with `engine.spawn("prefabs/coin.json", position)`. To change every coin, run the game, press `F2`, click a coin,
  edit it in the Properties panel (color, size, its script's `spin` and `bob`...) and press **Save as prefab**
  (set the path to `../assets/prefabs/coin.json` first: it suggests the entity's name). The next coins use it.
- `assets/input.json` maps keys and gamepad buttons to the actions and axes the scripts read.
- `project.json` says where the game starts: its scene, its game script and its input file.
- `bin/` is the engine's Lua runner, named after the game. To ship the game, copy the whole folder.

Script errors show on screen with their file and line, and the game keeps running.

Install the **Lua** extension (`sumneko.lua`; VS Code offers it when you open the folder) for autocomplete and type
hints on the engine's API in your scripts: `.luarc.json` and `types/engine.lua` set it up.
