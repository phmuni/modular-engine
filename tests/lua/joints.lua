-- joints: Entity:setJoint with a table, jointAngle, jointPosition, the motor calls and removeJoint from a script

test.run(function()
  physics.setGravity(vec3(0))

  -- A wall on a hinge at its -x end, turned by a velocity motor and stopped by a pillar
  local wall = engine.create("Wall", { model = "box", position = vec3(0, 1, 0), scale = vec3(4, 2, 0.3), body = "dynamic" })
  test.ok(not wall:hasJoint(), "no joint yet")
  wall:setJoint{
    type = "hinge",
    anchor = vec3(-0.5, 0, 0),
    axis = vec3(0, 1, 0),
    motor = "velocity",
    motorSpeed = 60,
  }
  test.ok(wall:hasJoint(), "the joint is there")
  wait(0.5)
  test.ok(wall:jointAngle() > 20 and wall:jointAngle() < 35, "turning at 60 degrees per second, at " .. wall:jointAngle())

  -- A pillar 60 degrees round, 3 units from the pivot: the wall stops short of it
  engine.create("Pillar", { model = "box", position = vec3(-2 + 3 * math.cos(math.rad(60)), 1, -3 * math.sin(math.rad(60))), scale = vec3(0.6, 2, 0.6), body = "static" })
  wait(2.5)
  test.ok(wall:jointAngle() > 40 and wall:jointAngle() < 56, "stopped by the pillar, at " .. wall:jointAngle())

  -- Steered from the script: stop, go to a pose, go the other way
  wall:stopJointMotor()
  wait(0.5)
  local resting = wall:jointAngle()
  wait(0.5)
  test.near(wall:jointAngle(), resting, 1.0, "no motor: it stays where it is (no gravity)")
  wall:setJointMotorSpeed(-45)
  wait(0.5)
  test.ok(wall:jointAngle() < resting - 10, "the velocity motor turns it back the other way")

  -- A light door that goes to a pose and holds it (a stiff spring: the default force limit is for heavy things)
  local door = engine.create("Door", { model = "box", position = vec3(10, 1, 0), scale = vec3(2, 2, 0.2), body = "dynamic", mass = 50 })
  door:setJoint{ anchor = vec3(-0.5, 0, 0), motor = "position", motorTarget = 25, springFrequency = 3, springDamping = 1 }
  wait(2)
  test.near(door:jointAngle(), 25, 1.0, "the position motor goes to 25 degrees")
  door:setJointMotorTarget(-30)
  wait(2)
  test.near(door:jointAngle(), -30, 1.0, "and to -30")
  door:stopJointMotor()

  -- A slider with limits and a target, with every key of the table that matters
  local lift = engine.create("Lift", { model = "box", position = vec3(20, 1, 0), scale = vec3(3, 0.4, 3), body = "dynamic", mass = 400 })
  lift:setJoint{
    type = "slider",
    axis = vec3(0, 1, 0),
    limits = true, min = 0, max = 2,
    motor = "position", motorTarget = 1.5,
    maxMotorForce = 50000,
    springFrequency = 6, springDamping = 1,
    collideConnected = false,
  }
  wait(2)
  test.near(lift:jointPosition(), 1.5, 0.05, "the slider goes to its target")
  lift:setJointMotorTarget(10)
  wait(2)
  test.near(lift:jointPosition(), 2, 0.05, "and not past its limit")

  -- A joint to another entity by name, and a pendulum that keeps its length
  local post = engine.create("Post", { model = "box", position = vec3(40, 3, 0), scale = vec3(0.4, 0.4, 0.4), body = "static" })
  local bob = engine.create("Bob", { model = "sphere", position = vec3(42, 3, 0), scale = vec3(0.5, 0.5, 0.5), body = "dynamic", shape = "sphere", mass = 2 })
  physics.setGravity(vec3(0, -9.81, 0))
  bob:setJoint{ type = "distance", target = "Post" }
  wait(1.5)
  test.near((bob.position - post.position):length(), 2, 0.05, "the pendulum keeps its length")
  test.ok(bob.position.y < 2.9, "and swings down")

  -- Taking the joint off frees the body
  bob:removeJoint()
  test.ok(not bob:hasJoint(), "the joint is gone")
  wait(1)
  test.ok(bob.position.y < 1, "and the bob falls")

  -- Wrong names are errors, with a message
  local ok, err = pcall(function() wall:setJoint{ type = "spring" } end)
  test.ok(not ok and tostring(err):find("hinge"), "an unknown type is an error that names the choices")
  ok, err = pcall(function() wall:setJoint{ motor = "turbo" } end)
  test.ok(not ok and tostring(err):find("velocity"), "an unknown motor is an error")
end)
