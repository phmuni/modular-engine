-- Animation events from Lua: onAnimationEvent(self, name, phase, layer) in the script of the entity with the machine,
-- and entity:inAnimationWindow(name) for anyone, a parent's script polling it included

local MODEL = "models/tentacle/tentacle.glb"
local MACHINE = "../tests/fixtures/animators/events.json"
local LISTENER = "../tests/fixtures/event_listener.lua"

local function joined(list)
  return table.concat(list, " ")
end

test.run(function()
  game.events = {}
  local e = engine.create("Tentacle", { model = MODEL, script = LISTENER })
  test.eq(e:inAnimationWindow("win"), false, "no machine")
  e:setAnimator(MACHINE)

  -- A clip that plays once: moments and a window, each with the phase and the layer, and what the query said
  e:trigger("once")
  wait(0.8)
  test.eq(e:inAnimationWindow("win"), true, "inside the window: 0.8 s of 2 is 0.4 of the clip")
  wait(1.9)
  test.eq(joined(game.events),
    "zero:fire@base=false win:begin@base=true half:fire@base=false win:end@base=false tail:begin@base=true " ..
    "one:fire@base=false tail:end@base=false", "name, phase, layer, and the query as the callback runs")
  test.eq(e:inAnimationWindow("win"), false, "closed")
  test.eq(e:inAnimationWindow("tail"), false, "closed at the end of the clip")

  -- A layer's window, and the base's moments in the same frame
  game.events = {}
  e:setAnimator(MACHINE)
  e:trigger("swing")
  wait(0.7)
  test.eq(joined(game.events), "lead:fire@upper=false hurt:begin@upper=true", "the layer's name comes last")
  test.eq(e:inAnimationWindow("hurt"), true, "in any layer")
  e:setAnimator("")
  wait(0.1)
  test.eq(joined(game.events), "lead:fire@upper=false hurt:begin@upper=true hurt:end@upper=false",
    "a removed machine closes what it had open")
  test.eq(e:inAnimationWindow("hurt"), false)

  -- The script that is not the machine's hears nothing, and polls instead: the model hangs under the entity
  game.events = {}
  local holder = engine.create("Holder", { script = LISTENER })
  local model = engine.create("Model", { model = MODEL })
  model:setParent(holder)
  model:setAnimator(MACHINE)
  model:trigger("windowed")
  wait(0.6)
  test.eq(model:inAnimationWindow("w"), true)
  test.eq(holder:inAnimationWindow("w"), false, "the query is about the entity's own machine")
  test.eq(#game.events, 0, "events are not passed up to the parent's script")
  wait(0.4)
  test.eq(model:inAnimationWindow("w"), false)
end)
