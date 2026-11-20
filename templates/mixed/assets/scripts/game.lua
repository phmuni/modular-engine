-- The whole game: the score on screen, the best score kept between runs, and the coins: a ring of them, and a new ring
-- once they are all picked up. Runs before the entities' scripts; its `game` table is shared with all of them.

local COINS = 8
local RING_RADIUS = 7

function onStart()
  ui.pauseMenu(true) -- Escape: Resume, Options (volumes, fullscreen, vsync) and Quit
  game.score = 0
  game.best = save.get("bestScore", 0) -- saved data: still there the next time the game runs
  ui.load("ui/hud.rml")
  ui.text("score", game.score)
  ui.text("best", game.best)
  spawnCoins()
end

function game.collect()
  game.score = game.score + 1
  game.coins = game.coins - 1
  ui.text("score", game.score)
  if game.score > game.best then
    game.best = game.score
    save.set("bestScore", game.best) -- written when the game closes; save.write() saves it right now
    ui.text("best", game.best)
  end
  if game.coins == 0 then
    print("All coins collected. Here come more.")
    if celebrate then celebrate() end -- a C++ function, in C++ + Lua projects
    spawnCoins()
  end
end

-- Every coin is a copy of prefabs/coin.json (edit it in the editor: F2, select a coin, change it, Save Prefab), so
-- changing that file changes them all. engine.spawn puts the copy at a position.
function spawnCoins()
  for i = 0, COINS - 1 do
    local angle = i / COINS * math.pi * 2
    engine.spawn("prefabs/coin.json", vec3(math.cos(angle) * RING_RADIUS, 0.8, math.sin(angle) * RING_RADIUS))
  end
  game.coins = COINS
end
