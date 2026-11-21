-- Drives an example game (the Lua, C++ and mixed templates have the same screens): main menu -> Options -> Back ->
-- Play -> pause menu (Resume, and its Options) -> game over (Try again, Main menu) -> Play again.
local smoke = dofile(os.getenv("SMOKE_DIR") .. "/common.lua") -- the folder of these scripts, set by smoke_templates.py

function onStart()
  smoke.run(function()
    -- Main menu, and its Options screen
    smoke.waitFor("[data-event-click*=play]", "the main menu")
    smoke.visitOptions()
    smoke.waitFor("[data-event-click*=play]", "the main menu again")

    -- Start the game: the main menu goes away
    smoke.press("play", "Play")
    smoke.gone("[data-event-click*=play]", "the main menu")

    -- The pause menu: open, visit Options, Resume
    ui.pauseMenu():open()
    smoke.frames(2)
    smoke.check(ui.pauseMenu():isOpen(), "the pause menu did not open")
    smoke.waitFor("[data-event-click*=resume]", "the pause menu's Resume")
    smoke.visitOptions()
    smoke.press("resume", "Resume")
    smoke.frames(2)
    smoke.check(not ui.pauseMenu():isOpen(), "the pause menu did not close")

    -- Game over: the clock runs out (time runs fast)
    time.setScale(30)
    smoke.waitFor("[data-event-click*=retry]", "the game over screen", 600)
    time.setScale(1)
    smoke.press("retry", "Try again")
    smoke.gone("[data-event-click*=retry]", "the game over screen")

    -- ... and back to the main menu from a second one
    time.setScale(30)
    smoke.waitFor("[data-event-click*=menu]", "the second game over screen", 600)
    time.setScale(1)
    smoke.press("'menu'", "Main menu")
    smoke.waitFor("[data-event-click*=play]", "the main menu after the game over")
    smoke.press("play", "Play")
  end, 3000)
end
