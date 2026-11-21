-- Drives an empty project, which has the pause menu and the Options screen only: open the pause menu, visit Options,
-- Back, Resume.
local smoke = dofile(os.getenv("SMOKE_DIR") .. "/common.lua") -- the folder of these scripts, set by smoke_templates.py

function onStart()
  smoke.run(function()
    smoke.frames(5)
    ui.pauseMenu():open()
    smoke.frames(2)
    smoke.check(ui.pauseMenu():isOpen(), "the pause menu did not open")
    smoke.waitFor("[data-event-click*=resume]", "the pause menu's Resume")
    smoke.visitOptions()
    smoke.press("resume", "Resume")
    smoke.frames(2)
    smoke.check(not ui.pauseMenu():isOpen(), "the pause menu did not close")
  end, 1500)
end
