-- A game script that listens to the pause menu (tests/cpp/pauseMenuTests.cpp reads what it saw from `game`)
game.opened = 0
game.closed = 0
game.action = ""

function onMenuOpened(menu)
  if menu == "pause" then
    game.opened = game.opened + 1
  end
end

function onMenuClosed(menu)
  if menu == "pause" then
    game.closed = game.closed + 1
  end
end

function onMenuAction(menu, name)
  if menu == "pause" then
    game.action = name
  end
end
