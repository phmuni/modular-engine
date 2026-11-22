-- The sky's lighting from Lua: engine.setEnvironmentIntensity / environmentIntensity (the pixels are checked by
-- cpp/ibl_*). The scene's Skybox keeps it.

test.run(function()
  wait()
  test.near(engine.environmentIntensity(), 1.0, 1e-6, "1 by default")
  engine.setEnvironmentIntensity(0.4)
  test.near(engine.environmentIntensity(), 0.4, 1e-6)
  engine.setEnvironmentIntensity(-2)
  test.near(engine.environmentIntensity(), 0.0, 1e-6, "not below 0")
  wait()
  engine.setSkyColor(vec3(0.3, 0.4, 0.5)) -- a sky of one color lights the scene evenly
  engine.setEnvironmentIntensity(2)
  wait()
  test.near(engine.environmentIntensity(), 2.0, 1e-6)
  test.ok(engine.find("Sky") ~= nil, "kept in the scene's Skybox")
end)
