-- toggles: the engine's switches by name, as engine.setToggle and engine.isToggled see them.

test.run(function()
  wait()

  -- The defaults a game starts with (the test host turns the free camera off, as the runner does)
  test.eq(engine.isToggled("freeCamera"), false, "the game drives the camera")
  test.eq(engine.isToggled("shadows"), true)

  engine.setToggle("freeCamera", true)
  test.eq(engine.isToggled("freeCamera"), true, "setToggle round-trips")
  engine.setToggle("freeCamera", false)
  test.eq(engine.isToggled("freeCamera"), false)

  engine.setToggle("shadows", false)
  wait() -- a frame drawn without shadows
  test.eq(engine.isToggled("shadows"), false)
  engine.setToggle("shadows", true)

  -- A wrong name is an error that says which names there are
  local ok, message = pcall(engine.setToggle, "flyMode", true)
  test.ok(not ok, "an unknown name is an error")
  test.ok(tostring(message):find("freeCamera", 1, true) ~= nil, "the error lists the known names")
  ok = pcall(engine.isToggled, "nope")
  test.ok(not ok)
end)
