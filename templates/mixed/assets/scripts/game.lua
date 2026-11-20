-- The rules: a main menu, thirty seconds to pick up a ring of coins, and a game over screen. The screens are
-- documents in assets/ui; their buttons call onMenuAction below. The player is C++ (src/main.cpp). Runs before the
-- entities' scripts; its `game` table is shared with all of them.

local COINS = 8
local RING_RADIUS = 7
local TIME_LIMIT = 30 -- seconds
local START = vec3(0, 0.6, 3)

local coins = {}         -- the coins in the level
local mainMenu, gameOver -- the menus, once shown

-- Every coin is a copy of prefabs/coin.json (edit it in the editor: F2, select a coin, change it, Save Prefab), so
-- changing that file changes them all. engine.spawn puts the copy at a position.
local function spawnCoins()
  for i = 0, COINS - 1 do
    local angle = i / COINS * math.pi * 2
    coins[i + 1] = engine.spawn("prefabs/coin.json", vec3(math.cos(angle) * RING_RADIUS, 0.8, math.sin(angle) * RING_RADIUS))
  end
  game.coins = COINS
end

-- The level as a round starts: a fresh ring of coins, the player at the start, the score and the clock at their start
local function resetLevel()
  for _, coin in ipairs(coins) do
    if coin.valid then coin:destroy() end
  end
  spawnCoins()
  engine.find("Player").position = START
  game.score, game.time = 0, TIME_LIMIT
  ui.text("score", 0)
  ui.text("best", game.best)
end

-- A menu pauses the game under it and takes the keys and the mouse; Escape does nothing in the main menu
local function showMainMenu()
  mainMenu = ui.menu("ui/mainMenu.rml", { escape = "none" })
  ui.text("menu-best", game.best)
end

local function finish(won)
  game.playing = false
  gameOver = ui.menu("ui/gameOver.rml", { escape = "none" })
  ui.text("result", won and "You win!" or "Time's up")
  ui.text("final-score", game.score)
  ui.text("final-best", game.best)
end

function onStart()
  ui.pauseMenu(true) -- Escape while playing: the menu in assets/ui/pauseMenu.rml (Resume, Options, Quit)
  ui.loadingScreen() -- assets/ui/loading.rml shows while a scene takes a moment to load (engine.loadScene)
  ui.load("ui/hud.rml")
  game.best = save.get("bestScore", 0) -- saved data: still there the next time the game runs
  game.playing = false
  resetLevel()
  showMainMenu()
end

-- What the buttons of the menus ask for: action('play') in mainMenu.rml, and so on
function onMenuAction(menu, name)
  if name == "play" then
    mainMenu:close()
    game.playing = true
  elseif name == "retry" then
    gameOver:close()
    resetLevel()
    game.playing = true
  elseif name == "menu" then
    gameOver:close()
    resetLevel()
    showMainMenu()
  end
end

-- The clock runs on game time, so it stands still under a menu
function onUpdate(dt)
  if not game.playing then return end
  game.time = game.time - dt
  ui.text("time", math.max(0, math.ceil(game.time)))
  if game.time <= 0 then finish(false) end
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
    if celebrate then celebrate() end -- a C++ function, in C++ + Lua projects
    finish(true)
  end
end
