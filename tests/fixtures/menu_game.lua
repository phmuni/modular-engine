-- A game script that listens to menus (tests/cpp/menuTests.cpp reads what it saw from `game`)
game.opened = ""
game.closed = ""
game.action = ""
game.pauseAction = ""

function onMenuOpened(menu)
  game.opened = game.opened .. menu .. ";"
end

function onMenuClosed(menu)
  game.closed = game.closed .. menu .. ";"
end

function onMenuAction(menu, name)
  game.action = menu .. ":" .. name
end

function onPauseMenuAction(name)
  game.pauseAction = name
end
