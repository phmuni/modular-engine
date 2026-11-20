-- The whole game: the score on screen, and new coins once they are all picked up. Runs before the entities'
-- scripts; its `game` table is shared with all of them.

function onStart()
  game.score = 0
  game.coins = game.coins or 0
  ui.load("ui/hud.rml")
  ui.text("score", game.score)
end

function game.collect()
  game.score = game.score + 1
  game.coins = game.coins - 1
  ui.text("score", game.score)
  if game.coins == 0 then
    print("All coins collected. Here come more.")
    if celebrate then celebrate() end -- a C++ function, in C++ + Lua projects
    spawnCoins()
  end
end

function spawnCoins()
  for i = 0, 7 do
    local angle = i / 8 * math.pi * 2
    engine.create("Coin", {
      model = "sphere", position = vec3(math.cos(angle) * 7, 0.6, math.sin(angle) * 7), scale = vec3(0.5),
      color = vec3(1, 0.78, 0.25), emission = vec3(1, 0.78, 0.25),
      collision = true, script = "scripts/coin.lua",
    })
  end
end
