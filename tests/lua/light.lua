-- light: the Light usertype's range, color and intensity are readable and writable (the scene's lights are in the file)

test.run(function()
  engine.loadScene("../tests/fixtures/scenes/legacy_lights_v1.json")
  local frames = 0
  while engine.isLoadingScene() and frames < 300 do
    wait()
    frames = frames + 1
  end
  test.eq(engine.isLoadingScene(), false, "the scene loaded")

  local lamp = engine.find("Lamp")
  test.ok(lamp ~= nil and lamp.light ~= nil, "the lamp has a Light")
  local light = lamp.light
  -- Converted from the old attenuation of the file's version (1, 0.09, 0.032; brightness 3 + 0.2): about 100 units
  test.ok(light.range > 50 and light.range < 200, "range from the old attenuation: " .. tostring(light.range))
  light.range = 12.5
  test.near(light.range, 12.5, 1e-5, "range written")
  test.near(engine.find("Lamp").light.range, 12.5, 1e-5, "and read again")
  light.intensity = 5
  test.near(light.intensity, 5)

  -- The scene's ambient took the suns' (the file had none of its own)
  local color, intensity = engine.ambient()
  test.ok(intensity > 0, "ambient from the directional lights' own")

  -- The intensity tween still works
  tween.to(lamp, "intensity", 1, 0.1)
  wait(0.3)
  test.near(light.intensity, 1, 0.01, "intensity tweened")
end)
