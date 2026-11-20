-- The pause menu from Lua: the options table, pause = false (the game keeps running under the menu), the callbacks the
-- game script defines, and ui.isPauseMenuOpen. What the document's buttons do (set, step, action...) and the input and
-- cursor options are in the C++ tests pause_menu_*.

local opened, closed = 0, 0

function onPauseMenuOpened()
  opened = opened + 1
end

function onPauseMenuClosed()
  closed = closed + 1
end

-- Frames, not seconds: game time stands still while the menu pauses the game
local function frames(count)
  for _ = 1, count do
    wait()
  end
end

test.run(function()
  wait()
  test.eq(ui.isPauseMenuOpen(), false)
  test.eq(ui.isPaused(), false)

  -- pause = false: opened, and the game goes on
  ui.pauseMenu({ pause = false, freeCursor = false, blockInput = false })
  frames(2)
  local start = time.elapsed()
  ui.openPauseMenu()
  frames(30)
  test.eq(ui.isPauseMenuOpen(), true)
  test.eq(ui.isPaused(), true)
  test.eq(opened, 1, "onPauseMenuOpened")
  test.near(time.elapsed() - start, 0.5, 0.05, "game time keeps advancing while the menu is open")
  ui.closePauseMenu()
  frames(1)
  test.eq(ui.isPauseMenuOpen(), false)
  test.eq(closed, 1, "onPauseMenuClosed")

  -- The defaults (an options table starts from them): opening pauses the game
  ui.pauseMenu({ enabled = true })
  frames(2)
  start = time.elapsed()
  ui.openPauseMenu()
  frames(30)
  test.eq(ui.isPauseMenuOpen(), true)
  test.eq(opened, 2)
  test.near(time.elapsed(), start, 0.02, "the game is paused")
  ui.closePauseMenu()
  frames(2)
  test.eq(closed, 2)
  test.ok(time.elapsed() > start, "and runs again after")

  -- Opening twice is one opening
  ui.openPauseMenu()
  ui.openPauseMenu()
  frames(1)
  test.eq(opened, 3)
  ui.closePauseMenu()
  ui.closePauseMenu()
  frames(1)
  test.eq(closed, 3)

  -- Starting on a screen of the document, and a document that is not there: the default one is used
  ui.openPauseMenu("options")
  frames(2)
  test.eq(ui.isPauseMenuOpen(), true)
  ui.closePauseMenu()
  frames(1)
  ui.pauseMenu({ document = "ui/no_such_pause_menu.rml" })
  ui.openPauseMenu()
  frames(2)
  test.eq(ui.isPauseMenuOpen(), true, "falls back to the default document, with a warning")
  ui.closePauseMenu()
  frames(1)

  -- ui.pauseMenu(false) turns Escape off; the menu still opens from code
  ui.pauseMenu(false)
  ui.openPauseMenu()
  frames(2)
  test.eq(ui.isPauseMenuOpen(), true)
  ui.closePauseMenu()
  frames(1)
  test.eq(ui.isPauseMenuOpen(), false)
end)
