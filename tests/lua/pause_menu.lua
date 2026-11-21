-- The pause menu from Lua: the options table, pause = false (the game keeps running under the menu), the callbacks the
-- game script defines (onMenuOpened and onMenuClosed with the menu "pause"), and ui.pauseMenu():isOpen(). What the document's buttons do (set, step, action...) and the input and
-- cursor options are in the C++ tests pause_menu_*.

local opened, closed = 0, 0

function onMenuOpened(menu)
  if menu == "pause" then
    opened = opened + 1
  end
end

function onMenuClosed(menu)
  if menu == "pause" then
    closed = closed + 1
  end
end

-- Frames, not seconds: game time stands still while the menu pauses the game
local function frames(count)
  for _ = 1, count do
    wait()
  end
end

test.run(function()
  wait()
  test.eq(ui.pauseMenu():isOpen(), false)
  test.eq(ui.pauseMenu():name(), "pause")

  -- pause = false: opened, and the game goes on
  ui.pauseMenu({ pause = false, freeCursor = false, blockInput = false })
  frames(2)
  local start = time.elapsed()
  ui.pauseMenu():open()
  frames(30)
  test.eq(ui.pauseMenu():isOpen(), true)
  test.eq(opened, 1, "onMenuOpened")
  test.near(time.elapsed() - start, 0.5, 0.05, "game time keeps advancing while the menu is open")
  ui.pauseMenu():close()
  frames(1)
  test.eq(ui.pauseMenu():isOpen(), false)
  test.eq(closed, 1, "onMenuClosed")

  -- The defaults (an options table starts from them): opening pauses the game
  ui.pauseMenu({ enabled = true })
  frames(2)
  start = time.elapsed()
  ui.pauseMenu():open()
  frames(30)
  test.eq(ui.pauseMenu():isOpen(), true)
  test.eq(opened, 2)
  test.near(time.elapsed(), start, 0.02, "the game is paused")
  ui.pauseMenu():close()
  frames(2)
  test.eq(closed, 2)
  test.ok(time.elapsed() > start, "and runs again after")

  -- Opening twice is one opening
  ui.pauseMenu():open()
  ui.pauseMenu():open()
  frames(1)
  test.eq(opened, 3)
  ui.pauseMenu():close()
  ui.pauseMenu():close()
  frames(1)
  test.eq(closed, 3)

  -- Starting on a screen of the document, and a document that is not there: the default one is used
  ui.pauseMenu():open("options")
  frames(2)
  test.eq(ui.pauseMenu():isOpen(), true)
  ui.pauseMenu():close()
  frames(1)
  ui.pauseMenu({ document = "ui/no_such_pause_menu.rml" })
  ui.pauseMenu():open()
  frames(2)
  test.eq(ui.pauseMenu():isOpen(), true, "falls back to the default document, with a warning")
  ui.pauseMenu():close()
  frames(1)

  -- ui.pauseMenu(false) turns Escape off; the menu still opens from code
  ui.pauseMenu(false)
  ui.pauseMenu():open()
  frames(2)
  test.eq(ui.pauseMenu():isOpen(), true)
  ui.pauseMenu():close()
  frames(1)
  test.eq(ui.pauseMenu():isOpen(), false)
end)
