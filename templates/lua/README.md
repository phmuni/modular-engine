# MyGame

A game on [modular-engine](https://github.com/phmuni/modular-engine), written in Lua: nothing to compile.

```bash
./bin/MyGame
```

- `assets/scripts/` holds the game's behavior. `game.lua` runs for the whole game; an entity's script (its Script
  component) runs for that entity: `onStart(self)`, `onUpdate(self, dt)`, `onContact(self, other, point, normal)`,
  `onOverlap(self, other)`. Save a script while the game runs and it reloads, keeping each entity's state.
- `assets/scenes/level.json` is the level. In the game, `F2` opens the editor: click to select, `W` `E` `R` `T` to move,
  rotate, scale and resize, the Properties panel to edit (and to give an entity a script), Settings to save.
  `Ctrl+P` freezes the game, plays it from there and puts the level back on Stop.
- `assets/input.json` maps keys and gamepad buttons to the actions and axes the scripts read.
- `project.json` says where the game starts: its scene, its game script and its input file.
- `bin/` is the engine's Lua runner, named after the game. To ship the game, copy the whole folder.

Script errors show on screen with their file and line, and the game keeps running.
