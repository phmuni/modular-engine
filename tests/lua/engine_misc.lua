-- engine: the current scene's path, model preloading and fonts

test.run(function()
  test.eq(engine.scenePath(), "", "no scene loaded yet")
  test.eq(engine.isLoadingScene(), false)

  -- A model read ahead of time is then created as usual
  engine.preload("models/box/box.obj")
  wait(0.2)
  local crate = engine.create("Crate", { model = "models/box/box.obj" })
  test.ok(crate:has("Model"), "a preloaded model creates an entity with it")

  engine.loadScene("scenes/physics.json")
  wait()
  wait(0.5)
  test.eq(engine.isLoadingScene(), false, "the switch is done")
  test.eq(engine.scenePath(), "scenes/physics.json", "the file that was loaded")

  test.eq(ui.loadFont("ui/fonts/LatoLatin-Bold.ttf"), true, "a font that exists")
  test.eq(ui.loadFont("ui/fonts/not_there.ttf", true), false, "a font that cannot be read")
end)
