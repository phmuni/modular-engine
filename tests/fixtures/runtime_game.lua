-- A game script for tests/cpp/runtimeTests.cpp: its onStart spawns two entities, like a game making its level
function onStart()
  game.starts = (game.starts or 0) + 1
  engine.create("Spawned", { model = "box", position = vec3(1, 0, 0) })
  engine.create("Spawned", { model = "box", position = vec3(2, 0, 0) })
end
