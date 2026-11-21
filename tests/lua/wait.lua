-- wait() in coroutines: sequencing, the time scale, engine.stop, a destroyed entity ending its routines, and errors

local FRAME = 1 / 60

test.expectError("routine boom")
test.expectError("wait() only works inside a coroutine")

test.run(function()
  -- Sequencing: steps happen in order, each after its wait
  local log = {}
  local t0 = time.elapsed()
  engine.run(function()
    log[#log + 1] = "a"
    wait(0.5)
    log[#log + 1] = "b"
    wait(0.5)
    log[#log + 1] = "c"
  end)
  test.eq(log, { "a" }, "runs up to its first wait at once")
  wait(0.3)
  test.eq(log, { "a" })
  wait(0.4) -- 0.7 s
  test.eq(log, { "a", "b" })
  wait(0.5) -- 1.2 s
  test.eq(log, { "a", "b", "c" })

  -- Two routines interleave on their own clocks
  local order = {}
  engine.run(function() wait(0.4) order[#order + 1] = "slow" end)
  engine.run(function() wait(0.1) order[#order + 1] = "fast" end)
  wait(0.6)
  test.eq(order, { "fast", "slow" })

  -- wait() with nothing is the next frame
  local frames = {}
  engine.run(function()
    frames[1] = test.frame()
    wait()
    frames[2] = test.frame()
  end)
  wait(0.1)
  test.eq(frames[2] - frames[1], 1)

  -- The time scale: a wait is game time
  time.setScale(0.5)
  local finished = nil
  local s0 = time.elapsed()
  engine.run(function()
    wait(1)
    finished = time.elapsed() - s0
  end)
  local f0 = test.frame()
  wait(1.2)
  time.setScale(1)
  test.near(finished, 1, 2 * FRAME, "a second of game time")
  test.ok(test.frame() - f0 >= 2 * 60, "which took two seconds of frames at half speed")

  -- engine.stop ends a routine for good
  local stopped = false
  local handle = engine.run(function()
    wait(0.2)
    stopped = true
  end)
  engine.stop(handle)
  wait(0.4)
  test.ok(not stopped, "a stopped routine does not go on")

  -- self:run ends with its entity, even in the middle of a wait
  local e = engine.create("Runner", { model = "box" })
  local after = false
  e:run(function()
    wait(0.3)
    after = true
  end)
  wait(0.1)
  e:destroy()
  wait(0.5)
  test.ok(not after, "a destroyed entity's routines end with it")
  -- ... while one of the game's own outlives it
  local gameRan = false
  engine.run(function()
    wait(0.2)
    gameRan = true
  end)
  local doomed = engine.create("Doomed", { model = "box" })
  doomed:destroy()
  wait(0.4)
  test.ok(gameRan, "engine.run routines do not belong to an entity")

  -- An entity's routine can use the entity
  local mover = engine.create("Mover", { model = "box" })
  mover:run(function()
    wait(0.2)
    mover.position = vec3(3, 0, 0)
  end)
  wait(0.4)
  test.near(mover.position, vec3(3, 0, 0))

  -- An error in a routine is reported (test.expectError above), ends only that routine, and the game goes on
  local survivor = false
  engine.run(function()
    wait(0.1)
    error("routine boom")
  end)
  engine.run(function()
    wait(0.3)
    survivor = true
  end)
  wait(0.5)
  test.ok(survivor, "other routines are not affected by the error")
  wait(0.1)
  -- wait() outside a coroutine is an error with a clear message (expected above), not a hang: a timer callback is
  -- not a coroutine
  time.after(0.1, function() wait(1) end)
  wait(0.3)
end)
