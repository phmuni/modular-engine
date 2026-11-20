-- Menus from Lua: ui.menu with its options, the handle (open, close, isOpen, show, name), the stack, the callbacks the
-- game script defines, and the loading screen. What the documents' buttons do (actions, open('path'), back) and
-- Escape are in the C++ tests in tests/cpp/menuTests.cpp.

local MAIN = "../tests/fixtures/ui/menu_main.rml"
local OTHER = "../tests/fixtures/ui/menu_options.rml"

local opened, closed, actions = "", "", ""

function onMenuOpened(menu)
  opened = opened .. menu .. ";"
end

function onMenuClosed(menu)
  closed = closed .. menu .. ";"
end

function onMenuAction(menu, name)
  actions = actions .. menu .. ":" .. name .. ";"
end

-- Frames, not seconds: game time stands still while a menu pauses the game
local function frames(count)
  for _ = 1, count do
    wait()
  end
end

test.run(function()
  wait()
  -- ui.menu opens it and returns the handle; its name is the file's
  local main = ui.menu(MAIN)
  frames(2)
  test.eq(main:name(), "menu_main")
  test.eq(main:isOpen(), true)
  test.eq(opened, "menu_main;")

  -- The default options pause the game
  test.eq(ui.isPaused(), false, "ui.isPaused is the pause menu's")
  main:close()
  frames(1)
  test.eq(main:isOpen(), false)
  test.eq(closed, "menu_main;")

  -- Options on the same menu (a menu is made once per name, its options replaced): nothing paused, escape as a word
  ui.menu(MAIN, { pause = false, freeCursor = false, blockInput = false, escape = "none" })
  frames(2)
  test.eq(main:isOpen(), true)
  test.eq(opened, "menu_main;menu_main;")
  local start = time.elapsed()
  frames(30)
  test.near(time.elapsed() - start, 0.5, 0.05, "pause = false: game time goes on")

  -- Opening again does nothing; the handle opens it later too, on one of its screens
  main:open()
  frames(1)
  test.eq(opened, "menu_main;menu_main;")
  main:close()
  frames(1)
  main:open("second")
  frames(2)
  test.eq(main:isOpen(), true)
  main:show("main")
  frames(1)
  main:close()
  frames(1)
  test.eq(main:isOpen(), false)

  -- A stack: the one opened last is on top; closing the one below closes both. `name` is what the callbacks get.
  ui.menu(MAIN)
  local extra = ui.menu(OTHER, { name = "extra" })
  frames(2)
  test.eq(extra:name(), "extra")
  test.eq(main:isOpen(), true)
  test.eq(extra:isOpen(), true)
  extra:close()
  frames(2)
  test.eq(extra:isOpen(), false)
  test.eq(main:isOpen(), true, "the one below stays")
  extra:open()
  frames(2)
  main:close()
  frames(2)
  test.eq(main:isOpen(), false)
  test.eq(extra:isOpen(), false, "what was opened over it closes too")
  test.eq(closed, "menu_main;menu_main;menu_main;extra;extra;menu_main;")

  -- A mistake in `escape` is reported, and the default is kept
  test.expectError("escape")
  local checked = ui.menu(MAIN, { escape = "sideways" })
  frames(2)
  test.eq(checked:isOpen(), true)
  checked:close()
  frames(1)

  -- The loading screen: off until asked for, a missing document leaves it off, and no load is no progress to wait for
  test.eq(engine.sceneLoadProgress(), 1)
  ui.loadingScreen({ document = "../tests/fixtures/ui/loading.rml", delay = 0, minimumTime = 0.1 })
  engine.loadScene("../tests/fixtures/loading_scene.json")
  test.eq(engine.isLoadingScene(), true)
  test.near(engine.sceneLoadProgress(), 0, 1e-6)
  while engine.isLoadingScene() do
    wait()
  end
  test.eq(engine.sceneLoadProgress(), 1)
  ui.loadingScreen(false)
end)
