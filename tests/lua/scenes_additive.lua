-- engine: several scenes open at once (loadSceneAdditive, unloadScene, loadedScenes, activeScene, setActiveScene)

local A = "../tests/fixtures/additive_a.json"
local B = "../tests/fixtures/additive_b.json"

local function waitLoaded()
  local frames = 0
  while engine.isLoadingScene() and frames < 300 do
    wait()
    frames = frames + 1
  end
  test.eq(engine.isLoadingScene(), false, "the load finished")
end

test.run(function()
  test.eq(engine.loadedScenes(), {}, "no scene open yet")

  engine.loadScene(A)
  waitLoaded()
  test.eq(engine.loadedScenes(), { A })
  test.eq(engine.activeScene(), A)
  test.eq(engine.scenePath(), A, "scenePath is the active scene")
  test.ok(engine.find("A Box") ~= nil, "A's entities are there")

  -- Added next to A: both there, A still the active one
  engine.loadSceneAdditive(B)
  test.eq(engine.isLoadingScene(), true, "it waits like loadScene")
  waitLoaded()
  test.eq(engine.loadedScenes(), { A, B }, "in the order they were opened")
  test.eq(engine.activeScene(), A)
  test.ok(engine.find("A Box") ~= nil and engine.find("B Box") ~= nil, "both scenes' entities")

  -- Opening what is open does nothing
  engine.loadSceneAdditive(B)
  waitLoaded()
  wait(0.1)
  test.eq(#engine.loadedScenes(), 2)
  test.eq(#engine.findAll("B Box"), 1)

  test.eq(engine.setActiveScene("nowhere.json"), false, "a scene that is not open")
  test.eq(engine.setActiveScene(B), true)
  test.eq(engine.scenePath(), B)
  test.eq(engine.setActiveScene(A), true)

  -- Unloading B takes its entities and leaves A's
  test.eq(engine.unloadScene("nowhere.json"), false)
  test.eq(engine.unloadScene(B), true)
  wait(0.1)
  test.eq(engine.loadedScenes(), { A })
  test.eq(engine.find("B Box"), nil, "B's entities are gone")
  test.ok(engine.find("A Box") ~= nil, "A's stay")
  test.eq(engine.unloadScene(A), false, "the only open scene stays")

  -- loadScene replaces everything
  engine.loadSceneAdditive(B)
  waitLoaded()
  test.eq(#engine.loadedScenes(), 2)
  engine.loadScene(B)
  waitLoaded()
  test.eq(engine.loadedScenes(), { B })
  test.eq(engine.find("A Box"), nil)
end)
