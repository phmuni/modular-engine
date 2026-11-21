-- antialiasing: MSAA, FXAA and FSR settings: the defaults, setting and clamping them, and changing them while frames
-- are drawn. Behavior only: no pixel is compared (the GPU decides what MSAA gives, and how many samples it offers).

test.run(function()
  wait()

  -- Defaults: MSAA 2x on, FXAA and FSR off
  test.eq(engine.msaa(), 2, "MSAA 2x by default")
  test.eq(engine.fxaa(), false, "FXAA off by default")
  test.eq(engine.fsr(), false, "FSR off by default")
  test.near(engine.fsrSharpness(), 0.2, 1e-6)

  -- The scene's size does not depend on MSAA
  local width, height = engine.renderResolution()
  for _, samples in ipairs({ 0, 4, 8, 2 }) do
    engine.setMsaa(samples)
    wait() -- frames are drawn with each count
    local w, h = engine.renderResolution()
    test.eq(w, width, "MSAA does not change the render resolution")
    test.eq(h, height)
    if samples == 0 then
      test.eq(engine.msaa(), 0, "off")
    else
      local used = engine.msaa()
      test.ok(used >= 2 and used <= samples, "the GPU's maximum may cap the count, never raise it")
      test.ok(used == 2 or used == 4 or used == 8, "a power of two")
    end
  end

  -- Other numbers snap down; 1 and below are off; far too many is capped at 8
  engine.setMsaa(1)
  test.eq(engine.msaa(), 0)
  engine.setMsaa(-3)
  test.eq(engine.msaa(), 0)
  engine.setMsaa(3)
  test.eq(engine.msaa(), 2, "3 snaps down to 2")
  engine.setMsaa(100)
  test.ok(engine.msaa() >= 2 and engine.msaa() <= 8)
  wait()

  -- FXAA
  engine.setFxaa(true)
  test.eq(engine.fxaa(), true)
  wait()
  engine.setFxaa(false)
  test.eq(engine.fxaa(), false)

  -- FSR sharpness is clamped to 0 to 2
  engine.setFsrSharpness(0.5)
  test.near(engine.fsrSharpness(), 0.5, 1e-6)
  engine.setFsrSharpness(-1)
  test.near(engine.fsrSharpness(), 0, 1e-6, "clamped from below")
  engine.setFsrSharpness(9)
  test.near(engine.fsrSharpness(), 2, 1e-6, "clamped from above")
  engine.setFsrSharpness(0.2)

  -- FSR on at half scale (upscales) and at full scale (does nothing); frames are drawn in both
  engine.setFsr(true)
  test.eq(engine.fsr(), true)
  engine.setRenderScale(0.5)
  wait()
  wait()
  engine.setFxaa(true)
  wait() -- FXAA then EASU then RCAS
  engine.setRenderScale(1)
  wait()
  engine.setRenderScale(2)
  wait()
  engine.setRenderScale(0.5)
  engine.setMsaa(4)
  wait()
  test.eq(engine.fsr(), true, "the setting stays on whatever the scale")
  engine.setFsr(false)
  engine.setFxaa(false)
  engine.setRenderScale(1)
  engine.setMsaa(2)
  wait()
  test.eq(engine.msaa(), 2)

  -- The pause menu's Options rows load with the document and open without errors
  ui.pauseMenu(true)
  wait()
  ui.pauseMenu():open()
  wait()
  wait()
  test.ok(ui.pauseMenu():isOpen(), "the menu opened with the antialiasing rows")
  ui.pauseMenu():close()
  wait()
end)
