-- settings: the player's options by name (get, set, step, choices, label, enabled, reset) and where they are saved.
-- The save keys are the "engine.*" ones the engine has always used, so saves made before still work.

test.expectError("there is no such setting")

test.run(function()
  wait()

  -- What the engine starts with (the tests run with the engine's defaults), for the resets below
  local start = {
    master = settings.get("masterVolume"),
    scale = settings.get("renderScale"),
    msaa = settings.get("msaa"),
    fsr = settings.get("fsr"),
    ssao = settings.get("ssao"),
  }

  -- The registry
  local names = settings.names()
  test.eq(#names, 13)
  for _, name in ipairs({ "masterVolume", "musicVolume", "effectsVolume", "fullscreen", "vsync", "resolution",
                          "renderScale", "msaa", "fxaa", "fsr", "fsrSharpness", "ssao", "ssaoQuality" }) do
    test.ok(settings.has(name), name .. " is a setting")
  end
  test.ok(not settings.has("nothing"))
  test.eq(settings.kind("masterVolume"), "number")
  test.eq(settings.kind("fxaa"), "bool")
  test.eq(settings.kind("msaa"), "choice")
  local min, max, step = settings.range("renderScale")
  test.eq(min, 0.5)
  test.eq(max, 2)
  test.near(step, 0.05, 1e-9)
  test.eq(settings.range("fxaa"), 0, "no range for a boolean")

  -- A number: applied, rounded to its step, clamped, written under the old key as 0.49 and not 0.4900000001
  test.ok(not save.has("engine.volume.master"), "nothing saved until a setting changes")
  test.eq(settings.set("masterVolume", 0.494), true)
  test.eq(settings.get("masterVolume"), 0.49)
  test.eq(save.get("engine.volume.master"), 0.49, "kept in the save data")
  test.eq(settings.label("masterVolume"), "49%")
  settings.set("masterVolume", 7)
  test.eq(settings.get("masterVolume"), 1, "clamped")
  settings.step("masterVolume", -1)
  test.eq(settings.get("masterVolume"), 0.99)
  settings.step("masterVolume", 1)
  settings.step("masterVolume", 1)
  test.eq(settings.get("masterVolume"), 1, "a number stops at its end")
  test.eq(settings.set("masterVolume", "loud"), false, "text that is not a number does not fit")
  test.eq(settings.get("masterVolume"), 1)
  settings.set("musicVolume", 0.25)
  settings.set("effectsVolume", 0.5)
  test.eq(save.get("engine.volume.music"), 0.25)
  test.eq(save.get("engine.volume.effects"), 0.5)

  -- Render scale and FSR's sharpness
  settings.set("renderScale", 0.75)
  test.near(engine.renderScale(), 0.75, 1e-6)
  test.eq(save.get("engine.renderScale"), 0.75)
  test.eq(settings.label("renderScale"), "75%")
  settings.set("fsrSharpness", 0.5)
  test.near(engine.fsrSharpness(), 0.5, 1e-6)
  test.eq(settings.label("fsrSharpness"), "0.50")
  test.eq(save.get("engine.fsrSharpness"), 0.5)

  -- A boolean
  settings.set("fxaa", true)
  test.eq(engine.fxaa(), true)
  test.eq(settings.get("fxaa"), true)
  test.eq(save.get("engine.fxaa"), true)
  test.eq(settings.label("fxaa"), "On")
  settings.step("fxaa")
  test.eq(engine.fxaa(), false, "step flips a boolean")
  test.eq(settings.label("fxaa"), "Off")
  test.eq(save.get("engine.fxaa"), false)

  -- A choice with numbers: MSAA, by value, by its text, snapped down, and stepped around
  test.eq(settings.choices("msaa"), { "Off", "MSAA 2x", "MSAA 4x", "MSAA 8x" })
  settings.set("msaa", 4)
  test.eq(engine.msaa(), 4)
  test.eq(settings.get("msaa"), 4)
  test.eq(settings.label("msaa"), "MSAA 4x")
  test.eq(save.get("engine.msaa"), 4)
  settings.set("msaa", "MSAA 2x")
  test.eq(engine.msaa(), 2, "by its text")
  settings.set("msaa", 3)
  test.eq(engine.msaa(), 2, "3 snaps down to 2")
  settings.step("msaa", -1)
  test.eq(engine.msaa(), 0)
  test.eq(settings.label("msaa"), "Off")
  settings.step("msaa", -1)
  test.ok(engine.msaa() >= 4, "down from Off goes around to the highest level the GPU has")
  settings.step("msaa", 1)
  test.eq(engine.msaa(), 0, "and up from there around to Off")
  test.eq(save.get("engine.msaa"), 0)

  -- A choice with text: ambient occlusion's quality, and what it depends on
  test.eq(settings.choices("ssaoQuality"), { "Low", "Medium", "High" })
  settings.set("ssaoQuality", "high")
  test.eq(settings.get("ssaoQuality"), "high")
  test.eq(settings.label("ssaoQuality"), "High")
  test.eq(save.get("engine.ssaoQuality"), "high")
  settings.set("ssaoQuality", "Low")
  test.eq(settings.get("ssaoQuality"), "low", "by its text")
  test.eq(settings.set("ssaoQuality", "ultra"), false, "not on the list")
  settings.set("ssao", true)
  test.eq(settings.enabled("ssaoQuality"), true)
  settings.set("ssao", false)
  test.eq(settings.enabled("ssaoQuality"), false, "quality does not matter with ambient occlusion off")
  test.eq(save.get("engine.ssao"), false)

  -- FSR's sharpness matters while FSR is on and the scene is drawn below full size
  settings.set("fsr", false)
  settings.set("renderScale", 0.5)
  test.eq(settings.enabled("fsrSharpness"), false, "FSR is off")
  settings.set("fsr", true)
  test.eq(settings.enabled("fsrSharpness"), true)
  settings.set("renderScale", 1)
  test.eq(settings.enabled("fsrSharpness"), false, "drawn at full size")

  -- The window's size: a list of sizes with the window's own on it; saved as the window's size keys
  local before = settings.get("resolution")
  test.ok(before:match("^%d+ x %d+$"), "a size as text: " .. tostring(before))
  test.eq(settings.label("resolution"), before)
  local listed = false
  for _, size in ipairs(settings.choices("resolution")) do
    listed = listed or size == before
  end
  test.ok(listed, "the window's own size is on the list")
  test.ok(#settings.choices("resolution") >= 2)
  settings.step("resolution", 1)
  local after = settings.get("resolution")
  test.ok(after ~= before, "the next size")
  local width, height = engine.windowSize()
  test.eq(after, width .. " x " .. height, "the window has it")
  test.eq(save.get("engine.window.width"), width)
  test.eq(save.get("engine.window.height"), height)
  -- A size by its text, from the list (the window's earlier size is not on it any more: a dragged size only lives in
  -- the list while the window has it)
  local other = settings.choices("resolution")[2]
  test.eq(settings.set("resolution", other), true)
  test.eq(settings.get("resolution"), other)
  test.eq(settings.set("resolution", "7 x 3"), false, "a size that is not on the list does not fit")

  -- reset: back to what the game started with, and out of the save data
  test.ok(save.has("engine.msaa"))
  settings.reset("msaa")
  test.eq(engine.msaa(), start.msaa, "the engine's default")
  test.ok(not save.has("engine.msaa"))
  settings.reset()
  test.eq(settings.get("masterVolume"), start.master)
  test.eq(settings.get("renderScale"), start.scale)
  test.eq(settings.get("fsr"), start.fsr)
  test.eq(settings.get("ssao"), start.ssao)
  test.ok(not save.has("engine.volume.master"))
  test.ok(not save.has("engine.window.width"))

  -- A name that is not a setting is reported, and gives nothing
  test.eq(settings.get("nothing"), nil)
end)
