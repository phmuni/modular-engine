-- physics: a dynamic body falls onto a static floor and stops, raycasts, velocity and impulse, gravity, a character

test.run(function()
  -- The floor's top face is at y = 0
  local floor = engine.create("Floor", { model = "box", position = vec3(0, -0.5, 0), scale = vec3(30, 1, 30), body = "static" })
  local ball = engine.create("Ball", { model = "sphere", position = vec3(0, 5, 0), body = "dynamic", shape = "sphere" })
  local crate = engine.create("Crate", { model = "box", position = vec3(4, 3, 0), body = "dynamic" })
  test.near(physics.gravity(), vec3(0, -9.81, 0), 0.01, "Earth's by default")

  -- Falling: a drop of 0.5 s is 0.5 * g * t^2 = 1.2 m
  wait(0.5)
  test.ok(ball.position.y < 4.2 and ball.position.y > 3.4, "falling: " .. ball.position.y)
  test.ok(ball.velocity.y < -3, "moving down: " .. ball.velocity.y)

  -- It lands on the floor and stops, resting on it
  wait(3)
  test.near(ball.position.y, 1.0, 0.1, "resting on the floor, its radius (1 for the engine's sphere) above it")
  test.near(ball.velocity:length(), 0, 0.1, "at rest")
  test.near(crate.position.y, 0.5, 0.1, "the crate too")
  test.near(floor.position, vec3(0, -0.5, 0), 0.0001, "a static body does not move")

  -- Raycasts: straight down onto the floor, and one that hits nothing
  local hit = physics.raycast(vec3(10, 10, 10), vec3(0, -1, 0), 50)
  test.ok(hit ~= nil, "hits the floor")
  test.eq(hit.entity, floor)
  test.near(hit.point, vec3(10, 0, 10), 0.01)
  test.near(hit.normal, vec3(0, 1, 0), 0.01)
  test.near(hit.distance, 10, 0.01)
  test.ok(physics.raycast(vec3(10, 10, 10), vec3(0, 1, 0), 50) == nil, "nothing above")
  test.ok(physics.raycast(vec3(10, 10, 10), vec3(0, -1, 0), 5) == nil, "out of reach")
  local onBall = physics.raycast(vec3(0, 5, 0), vec3(0, -1, 0), 50)
  test.eq(onBall.entity, ball, "the nearest thing along the ray")
  test.near(onBall.point.y, ball.position.y + 1.0, 0.1, "the top of the sphere")

  -- A velocity set from a script moves a body; an impulse changes it
  ball.velocity = vec3(0, 6, 0)
  test.near(ball.velocity, vec3(0, 6, 0), 0.01)
  wait(0.2)
  test.ok(ball.position.y > 1.0, "thrown up")
  wait(2)
  test.near(ball.position.y, 1.0, 0.1, "and back down")
  crate:impulse(vec3(0, 0, 2000))
  wait(0.1)
  test.ok(crate.velocity.z > 0.1, "an impulse pushes: " .. crate.velocity.z)

  -- Gravity can be changed
  physics.gravity(vec3(0, 0, 0))
  test.near(physics.gravity(), vec3(0, 0, 0), 0.0001)
  local floater = engine.create("Floater", { model = "sphere", position = vec3(-5, 8, 0), body = "dynamic", shape = "sphere" })
  wait(1)
  test.near(floater.position.y, 8, 0.2, "no gravity, no fall")
  physics.gravity(vec3(0, -9.81, 0))

  -- A character falls, lands (onGround) and walks when told to
  local hero = engine.create("Hero", { position = vec3(-10, 2, 0), character = true })
  test.ok(not hero.onGround or hero.position.y > 1, "starts in the air")
  wait(1.5)
  test.ok(hero.onGround, "landed")
  test.near(hero.position.y, 0.9, 0.2, "standing on the floor, its middle half its height up")
  local x0 = hero.position.x
  hero:move(vec3(2, 0, 0))
  wait(1)
  test.near(hero.position.x - x0, 2, 0.4, "walked about 2 m in a second at 2 m/s")
  hero:move(vec3(0, 0, 0))
  wait(0.3)
  local stopped = hero.position.x
  wait(0.3)
  test.near(hero.position.x, stopped, 0.01, "and stopped when told to")
end)
