-- A game script that listens to the pause menu (tests/cpp/pauseMenuTests.cpp reads what it saw from `game`)
game.opened = 0
game.closed = 0
game.action = ""

function onPauseMenuOpened()
  game.opened = game.opened + 1
end

function onPauseMenuClosed()
  game.closed = game.closed + 1
end

function onPauseMenuAction(name)
  game.action = name
end
