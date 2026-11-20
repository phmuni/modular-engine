-- ssao: ambient occlusion is on by default at medium quality, its settings clamp, and changing it between frames
-- (the depth prepass and AO textures are made, resized and dropped as it goes) keeps the game running.

test.run(function()
  wait() -- the first frame, drawn with the defaults

  test.eq(engine.ssao(), true, "on by default")
  local settings = engine.ssaoSettings()
  test.eq(settings.quality, "medium", "medium quality by default")
  test.eq(settings.halfResolution, true, "half resolution by default")
  test.near(settings.radius, 0.5, 1e-6, "default radius")
  test.near(settings.intensity, 1.0, 1e-6, "default intensity")

  engine.setSsao(false)
  test.eq(engine.ssao(), false)
  wait()
  engine.setSsao(true)
  test.eq(engine.ssao(), true)
  wait()

  -- Only the fields given change
  engine.setSsaoSettings { radius = 1.25 }
  settings = engine.ssaoSettings()
  test.near(settings.radius, 1.25, 1e-6)
  test.near(settings.intensity, 1.0, 1e-6, "intensity untouched")
  test.eq(settings.quality, "medium", "quality untouched")

  -- Clamped to the limits
  engine.setSsaoSettings { radius = 1000, intensity = 100 }
  settings = engine.ssaoSettings()
  test.near(settings.radius, 10.0, 1e-6, "radius clamped from above")
  test.near(settings.intensity, 4.0, 1e-6, "intensity clamped from above")
  engine.setSsaoSettings { radius = -1, intensity = -1 }
  settings = engine.ssaoSettings()
  test.near(settings.radius, 0.05, 1e-6, "radius clamped from below")
  test.near(settings.intensity, 0.0, 1e-6, "intensity clamped from below")
  wait() -- an intensity of 0 draws without AO

  -- Quality and resolution, with frames drawn in between
  engine.setSsaoSettings { radius = 0.5, intensity = 1 }
  for _, quality in ipairs { "low", "high", "medium" } do
    engine.setSsaoSettings { quality = quality }
    test.eq(engine.ssaoSettings().quality, quality)
    wait()
  end
  engine.setSsaoSettings { quality = "LOW" }
  test.eq(engine.ssaoSettings().quality, "low", "any case")
  engine.setSsaoSettings { quality = "ultra" } -- not a quality: ignored
  test.eq(engine.ssaoSettings().quality, "low")
  engine.setSsaoSettings { halfResolution = false }
  test.eq(engine.ssaoSettings().halfResolution, false)
  wait()
  engine.setRenderScale(0.5) -- the targets follow the render size
  wait()
  engine.setSsaoSettings { halfResolution = true }
  engine.setRenderScale(1)
  wait()
  test.eq(engine.ssaoSettings().halfResolution, true)
end)
