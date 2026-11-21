-- onContactEnd: called on both entities' scripts once per pair of bodies when they stop touching, however many parts of
-- their shapes touched; characters count like they do for onContact; nothing is reported for an entity destroyed while
-- it touches (and nothing crashes)

local SCRIPT = "../tests/fixtures/contact_listener.lua"

test.run(function()
  local floor = engine.create("Floor", { model = "box", position = vec3(0, -0.5, 0), scale = vec3(30, 1, 30),
                                         body = "static", script = SCRIPT })
  local ball = engine.create("Ball", { model = "sphere", position = vec3(-8, 3, 0), body = "dynamic",
                                       shape = "sphere", script = SCRIPT })
  wait(1.5)
  test.eq(ball.begins, 1, "the ball landed once")
  test.eq(floor.begins, 1, "the floor was told too")
  test.eq(ball.ends, 0, "still touching")
  test.eq(ball.lastBegin, "Floor")

  -- Nudged awake while it rests (it has gone to sleep by now: Jolt drops the contacts of sleeping bodies): the same
  -- contact goes on, with no end and no second begin
  ball.velocity = vec3(0.05, 0, 0)
  wait(0.3)
  test.eq(ball.begins, 1, "waking a resting body is not a new contact")
  test.eq(ball.ends, 0, "nor the end of one")

  -- Lifted away: both are told it ended, once
  ball.position = vec3(-8, 4, 0)
  ball.velocity = vec3(0, 0, 0)
  wait(0.1)
  test.eq(ball.ends, 1, "ball: onContactEnd once")
  test.eq(floor.ends, 1, "floor: onContactEnd once")
  test.eq(ball.lastEnd, "Floor")
  test.eq(floor.lastEnd, "Ball")
  wait(0.3)
  test.eq(ball.ends, 1, "and not again while it falls")
  wait(1)
  test.eq(ball.begins, 2, "it lands again")

  -- A destroyed entity ends its contacts without a call, and the one it touched is not told
  local before = floor.ends
  ball:destroy()
  wait(0.3)
  test.eq(floor.ends, before, "nothing for an entity destroyed while touching")
  local fresh = engine.create("Fresh", { model = "box", position = vec3(10, 5, 0), script = SCRIPT })
  wait(0.3)
  test.eq(fresh.ends, 0, "an entity made after it is not told of the old pair")
  test.eq(floor.ends, before)

  -- Many parts touching are one contact: a box lying on a mesh body (two triangles of its top face, at least) begins and
  -- ends once
  local ground = engine.create("MeshGround", { model = "box", position = vec3(0, 0.25, 12), scale = vec3(8, 0.5, 8),
                                               body = "static", shape = "mesh", script = SCRIPT })
  local crate = engine.create("Crate", { model = "box", position = vec3(0, 1.5, 12), scale = vec3(1, 1, 1),
                                         body = "dynamic", script = SCRIPT })
  wait(1)
  test.eq(crate.begins, 1, "one begin for the whole face")
  test.eq(ground.begins, 1)
  crate.position = vec3(0, 3, 12)
  crate.velocity = vec3(0, 0, 0)
  wait(0.1)
  test.eq(crate.ends, 1, "one end")
  test.eq(ground.ends, 1)

  -- A character is a body for contacts too: a flat box resting on its head begins, and ends when it walks away
  local hero = engine.create("Hero", { position = vec3(-8, 0.9, -10), character = true, script = SCRIPT })
  wait(0.5)
  local lid = engine.create("Lid", { model = "box", position = vec3(-8, 2.2, -10), scale = vec3(1.2, 0.2, 1.2),
                                     body = "dynamic", script = SCRIPT })
  wait(1.5)
  test.eq(hero.begins, 1, "the character is told it was landed on")
  test.eq(lid.begins >= 1, true)
  test.eq(hero.lastBegin, "Lid")
  hero:move(vec3(4, 0, 0))
  wait(1.5)
  test.eq(hero.ends, 1, "and that it was left")
  test.eq(hero.lastEnd, "Lid")
  test.ok(lid.ends >= 1)
end)
