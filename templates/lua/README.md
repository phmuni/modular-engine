# MyGame

A game on [modular-engine](https://github.com/phmuni/modular-engine), written in Lua: nothing to compile.

```bash
./bin/MyGame
```

- `assets/scripts/` holds the game's behavior. `game.lua` runs for the whole game; an entity's script (its Script
  component) runs for that entity: `onStart(self)`, `onUpdate(self, dt)`, `onContact(self, other, point, normal)`,
  `onOverlap(self, other)`. Save a script while the game runs and it reloads, keeping each entity's state. Its
  `properties` table (speed, jumpSpeed...) is what the Properties panel lets you tune, per entity.
- The camera follows the player with one call, `camera.follow(self, { offset = vec3(0, 4, 7), lookAhead = 2, smoothing = 0.1 })`
  in `player.lua`: the engine moves it after the physics and the scripts, so it does not jitter. `coin.lua` shrinks a
  picked-up coin away with `tween.to(...)`; `engine.run(function() ... wait(1) ... end)` and `self:run(...)` give a
  script waits. The player lives in `level.json` (select it in the editor to tune its script's properties); a game
  that makes it from code can use `engine.create("Player", { model = "box", character = { radius = 0.4, height = 0.8 } })`.
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
- `project.json` says where the game starts: its scene, its game script and its input file. An optional `"window": { "width": 1280, "height": 720, "fullscreen": false, "renderScale": 1.0 }` sets the window and the 3D render scale. An optional `"graphics": { "msaa": 2, "fxaa": false, "fsr": false, "fsrSharpness": 0.2 }` sets antialiasing (MSAA 0, 2, 4 or 8, FXAA) and FSR upscaling (used when the render scale is below 1).
- `bin/` is the engine's Lua runner, named after the game. To ship the game, copy the whole folder.

Script errors show on screen with their file and line, and the game keeps running.

Install the **Lua** extension (`sumneko.lua`; VS Code offers it when you open the folder) for autocomplete and type
hints on the engine's API in your scripts: `.luarc.json` and `types/engine.lua` set it up.
