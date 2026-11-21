-- time: after, every, cancel, the scale, pause and the game clock. Frames are a fixed 1/60 s, so a timer of 0.5 s
-- fires about 30 frames in (within one frame either way).

local FRAME = 1 / 60

test.run(function()
  local start = time.elapsed()
  local firedAt = {}
  time.after(0.5, function() firedAt.after = time.elapsed() end)
  local ticks = 0
  local every = time.every(0.25, function() ticks = ticks + 1 end)
  local cancelled = false
  local doomed = time.after(0.3, function() cancelled = true end)
  time.cancel(doomed)

  wait(0.2)
  test.eq(firedAt.after, nil, "not yet")
  test.eq(ticks, 0)
  wait(0.4) -- 0.6 s in
  test.near(firedAt.after - start, 0.5, 2 * FRAME, "after fires once, on time")
  test.eq(ticks, 2, "every 0.25 s: twice by 0.6 s")
  wait(0.5) -- 1.1 s in
  test.eq(ticks, 4)
  test.ok(not cancelled, "a cancelled timer never fires")
  time.cancel(every)
  wait(0.6)
  test.eq(ticks, 4, "a cancelled repeating timer stops")

  -- The clock follows the frames
  test.near(time.delta(), FRAME, 0.0001)
  local t0 = time.elapsed()
  wait(1)
  test.near(time.elapsed() - t0, 1, 2 * FRAME)

  -- The time scale slows everything on game time: timers, the clock, waits
  test.eq(time.scale(), 1)
  time.setScale(0.5)
  test.eq(time.scale(), 0.5)
  local slowFired = nil
  local s0 = time.elapsed()
  time.after(1, function() slowFired = time.elapsed() - s0 end) -- 1 s of game time, 2 s of real frames
  local frames0 = test.frame()
  wait(0.9)
  test.eq(slowFired, nil)
  wait(0.2)
  test.near(slowFired, 1, 2 * FRAME, "fires after a game second")
  local frames = test.frame() - frames0
  test.near(frames, 2 * 60 * 1.1, 3, "at half speed 1.1 s of game time takes 132 frames")
  test.near(time.delta(), FRAME * 0.5, 0.0001)
  time.setScale(1)

  -- Paused: nothing moves, timers hold their remaining time
  local pausedFired = false
  time.after(0.2, function() pausedFired = true end)
  time.setPaused(true)
  test.eq(time.isPaused(), true)
  local frozen = time.elapsed()
  local pausedFrames = test.frame()
  while test.frame() - pausedFrames < 30 do
    coroutine.yield() -- real frames: a wait() would be on game time, which is stopped
  end
  test.eq(time.elapsed(), frozen, "the clock stands still")
  test.ok(not pausedFired, "so do timers")
  time.setPaused(false)
  test.eq(time.isPaused(), false)
  wait(0.3)
  test.ok(pausedFired, "and they go on afterwards")
end)
