-- An entity script from engine.create: onStart once, onUpdate every frame, properties with defaults, self:run

test.run(function()
  local mover = engine.create("Mover", { model = "box", script = "../tests/fixtures/mover.lua" })
  test.ok(mover:has("Script"))
  test.eq(mover.script, mover, "script gives the entity when it has one")
  test.ok(engine.create("Plain", { model = "box" }).script == nil)

  wait(1)
  test.eq(mover.started, 1, "onStart ran once")
  test.ok(mover.updates >= 55 and mover.updates <= 62, "onUpdate every frame: " .. tostring(mover.updates))
  test.eq(mover.speed, 2, "the property's default")
  test.near(mover.position.x, 2, 0.1, "moved at 2 units a second for a second")
  test.eq(mover.routineRan, true, "self:run inside onStart")

  -- Destroyed: the script stops with it
  mover:destroy()
  wait(0.2)
  test.eq(mover.valid, false)
end)
