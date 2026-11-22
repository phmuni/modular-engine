-- Reflection probes from Lua: the scene's probe is captured when it loads, refreshed in memory, baked to its file (which
-- is removed again), and reports when it is out of date (the pixels are checked by tests/cpp/reflectionProbeTests.cpp)

local ROOM = "../tests/fixtures/probe_room.json"
local FILE = "../tests/fixtures/_probe_out/room.probe"
local function exists() return io.open(FILE, "rb") or io.open("bin/" .. FILE, "rb") or io.open("tests/fixtures/_probe_out/room.probe", "rb") end

local function waitLoaded()
  local frames = 0
  while engine.isLoadingScene() and frames < 300 do
    wait()
    frames = frames + 1
  end
end

test.run(function()
  os.remove(FILE); os.remove("tests/fixtures/_probe_out/room.probe")
  engine.loadScene(ROOM)
  waitLoaded()
  wait(0.1)
  local probe = engine.find("Room Probe")
  test.ok(probe ~= nil and probe:has("ReflectionProbe"), "the probe is in the scene")
  test.eq(engine.reflectionProbeCount(), 1)
  test.eq(engine.reflectionProbeCaptures(), 1, "no file: captured at load")
  test.eq(probe:isReflectionProbeStale(), false)

  test.eq(probe:refreshReflectionProbe(), true)
  test.eq(engine.reflectionProbeCaptures(), 2, "refreshed in memory")
  test.eq(exists(), nil, "refresh writes no file")

  probe.position = vec3(1, 2, 0)
  test.eq(probe:isReflectionProbeStale(), true, "moved since it was captured")

  test.eq(probe:bakeReflectionProbe(), true)
  local f = exists()
  test.ok(f ~= nil, "bake wrote the file")
  if f then f:close() end
  test.eq(probe:isReflectionProbeStale(), false)
  test.eq(engine.bakeReflectionProbes(), 1)
  test.eq(engine.find("Room Floor"):bakeReflectionProbe(), false, "not a probe")
  os.remove(FILE); os.remove("tests/fixtures/_probe_out/room.probe")
end)
