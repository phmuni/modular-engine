-- tween: end values, easing curves, completion, cancel, and a tween on an entity that is destroyed meanwhile

-- The easings as formulas, to compare the engine's with
local eases = {
  linear = function(t) return t end,
  easeInQuad = function(t) return t * t end,
  easeOutQuad = function(t) return 1 - (1 - t) * (1 - t) end,
  easeInOutQuad = function(t) return t < 0.5 and 2 * t * t or 1 - (-2 * t + 2) ^ 2 / 2 end,
  easeInCubic = function(t) return t * t * t end,
  easeOutCubic = function(t) return 1 - (1 - t) ^ 3 end,
  easeInOutCubic = function(t) return t < 0.5 and 4 * t * t * t or 1 - (-2 * t + 2) ^ 3 / 2 end,
}

test.expectError("unknown easing")

test.run(function()
  -- Each easing against its formula, sampled every frame: a linear tween of the same length gives the progress t (the
  -- tweens of a frame run in the order they were made). The one nearest the middle is kept, to spell out the midpoints.
  local results = {}
  for name, formula in pairs(eases) do
    local result = { progress = 0, worst = 0, samples = 0, final = nil, mid = nil }
    results[name] = result
    tween.value(0, 1, 1, "linear", function(v) result.progress = v end)
    tween.value(0, 1, 1, name, function(v)
      result.worst = math.max(result.worst, math.abs(v - formula(result.progress)))
      result.samples = result.samples + 1
      result.final = v
      if not result.mid or math.abs(result.progress - 0.5) < math.abs(result.mid.t - 0.5) then
        result.mid = { t = result.progress, v = v }
      end
    end)
  end
  wait(1.2)
  for name, result in pairs(results) do
    test.ok(result.samples >= 55, name .. " was updated every frame (" .. result.samples .. ")")
    test.near(result.worst, 0, 0.001, name .. " follows its curve")
    test.near(result.final, 1, 0.0001, name .. " ends at 1")
  end
  -- The midpoints (the sample nearest t = 0.5 is within a frame of it)
  test.near(results.linear.mid.v, 0.5, 0.02)
  test.near(results.easeInQuad.mid.v, 0.25, 0.02)
  test.near(results.easeOutQuad.mid.v, 0.75, 0.02)
  test.near(results.easeInOutQuad.mid.v, 0.5, 0.02)
  test.near(results.easeInCubic.mid.v, 0.125, 0.02)
  test.near(results.easeOutCubic.mid.v, 0.875, 0.02)
  test.near(results.easeInOutCubic.mid.v, 0.5, 0.02)

  -- A number from `from` to `to`, and the completion callback once
  local last, completed = nil, 0
  tween.value(10, 20, 0.5, "linear", function(v) last = v end, function() completed = completed + 1 end)
  wait(0.3)
  test.ok(last > 10 and last < 20, "in between")
  test.eq(completed, 0)
  wait(0.4)
  test.near(last, 20, 0.0001, "ends exactly at the target")
  test.eq(completed, 1)
  wait(0.3)
  test.eq(completed, 1, "completion is reported once")

  -- Entities: position, rotation, scale, color end at their targets
  local e = engine.create("Tweened", { model = "box", position = vec3(0, 0, 0) })
  local done = false
  tween.to(e, "position", vec3(4, 2, 0), 1, "linear", function() done = true end)
  wait(0.5)
  local mid = e.position
  test.near(mid.x, 2, 0.1, "halfway along a linear tween")
  test.near(mid.y, 1, 0.1)
  test.ok(not done)
  wait(0.6)
  test.near(e.position, vec3(4, 2, 0), 0.0001)
  test.ok(done, "onComplete ran")

  tween.to(e, "rotation", vec3(0, 90, 0), 0.5, "easeOutQuad")
  tween.to(e, "scale", 3, 0.5, "easeInOutCubic") -- a number is all three axes
  tween.to(e, "color", vec3(0, 1, 0), 0.5)
  wait(0.6)
  test.near(e.rotation, vec3(0, 90, 0), 0.01)
  test.near(e.scale, vec3(3, 3, 3), 0.0001)
  test.near(e:color(), vec3(0, 1, 0), 0.0001)

  -- An easing curve shows on an entity: ease-in is behind linear at the middle
  local slow = engine.create("Slow", { position = vec3(0, 0, 0) })
  tween.to(slow, "position", vec3(10, 0, 0), 1, "easeInQuad")
  wait(0.5)
  test.near(slow.position.x, 2.5, 0.6, "a quarter of the way at half time")
  wait(0.6)
  test.near(slow.position.x, 10, 0.0001)

  -- Cancel: it stays where it was, and does not report completing
  local c = engine.create("Cancelled", { position = vec3(0, 0, 0) })
  local reported = false
  local handle = tween.to(c, "position", vec3(10, 0, 0), 1, "linear", function() reported = true end)
  wait(0.3)
  tween.cancel(handle)
  local stopped = c.position.x
  wait(0.5)
  test.eq(c.position.x, stopped, "a cancelled tween stops moving")
  test.ok(not reported)
  test.ok(stopped > 0.5 and stopped < 5)

  -- The time scale and pause apply: a tween is on game time
  local s = engine.create("Scaled", { position = vec3(0, 0, 0) })
  time.setScale(0.5)
  tween.to(s, "position", vec3(10, 0, 0), 1)
  wait(1) -- one game second at half speed
  test.near(s.position.x, 10, 0.5, "done after one game second")
  time.setScale(1)

  -- An entity destroyed mid-tween: the tween ends without crashing, and nothing is revived
  local gone = engine.create("Gone", { model = "box" })
  local goneDone = false
  tween.to(gone, "position", vec3(5, 5, 5), 0.5, "linear", function() goneDone = true end)
  wait(0.2)
  gone:destroy()
  wait(0.5)
  test.eq(gone.valid, false)

  -- An unknown easing name is an error (expected above), not a silent linear
  engine.run(function() tween.to(e, "position", vec3(0, 0, 0), 0.5, "wobble") end) -- in its own routine, which it ends
  wait(0.1)
  test.near(e.position, vec3(4, 2, 0), 0.0001, "nothing was tweened")
end)
