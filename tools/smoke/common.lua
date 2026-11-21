-- Helpers for the smoke test scripts (tools/smoke_templates.py runs them with the game's --script option). Each
-- script loads this one with dofile. A failed check raises an error, which the engine logs as a script error: the
-- smoke test fails on it (and on the line "SMOKE OK" never appearing).

local smoke = {}

-- Frames, not seconds: game time stands still under a menu
function smoke.frames(count)
  for _ = 1, count do
    wait()
  end
end

function smoke.check(condition, message)
  if not condition then
    error("smoke: " .. message, 2)
  end
end

-- Waits (at most `limit` frames) until an element matching the selector is shown
function smoke.waitFor(selector, what, limit)
  for _ = 1, limit or 120 do
    if ui.isVisible(selector) then
      return
    end
    wait()
  end
  error("smoke: never saw " .. what .. " (" .. selector .. ")", 2)
end

-- Clicks the button of the screen on top (its data-event-click contains `action`), as a player would
function smoke.press(action, what)
  smoke.waitFor("[data-event-click*=" .. action .. "]", what)
  smoke.check(ui.click("[data-event-click*=" .. action .. "]"), "could not click " .. what)
  smoke.frames(3)
end

function smoke.gone(selector, what)
  smoke.frames(2)
  smoke.check(not ui.isVisible(selector), what .. " is still shown")
end

-- The Options screen (over the main menu or the pause menu): changes an option and puts it back, then Back
function smoke.visitOptions()
  smoke.press("open('ui/options.rml')", "the Options button")
  smoke.waitFor("[data-event-click*=back]", "the Options screen")
  smoke.press("toggle('fxaa')", "the FXAA toggle")
  smoke.press("toggle('fxaa')", "the FXAA toggle")
  smoke.press("step('ssaoQuality', 1)", "the SSAO quality step")
  smoke.press("step('ssaoQuality', -1)", "the SSAO quality step")
  smoke.press("back()", "Back")
  smoke.gone("[data-event-click*=back]", "the Options screen")
end

-- Runs the script's steps as a routine. A failed step ends the game at once (a hang would only show at the test's own
-- timeout) and still raises its error, which the engine logs and the test reports; a script that waits for something
-- that never happens is caught after `limit` frames.
function smoke.run(steps, limit)
  engine.run(function()
    smoke.frames(limit)
    error("smoke: the script did not finish in " .. limit .. " frames")
  end)
  engine.run(function()
    local ok, problem = pcall(steps)
    if ok then
      print("SMOKE OK") -- the marker the test looks for
      engine.quit()
    else
      engine.quit()
      error(problem, 0)
    end
  end)
end

return smoke
