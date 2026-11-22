-- physics spin: angularVelocity (degrees per second), torque, angularImpulse, impulseAt and forceAt from a script

test.run(function()
  physics.setGravity(vec3(0))

  -- A sphere of 2 kg and radius 1 (inertia 0.8 kg m^2), with no damping so its spin lasts
  local function sphere(name, position)
    local e = engine.create(name, { model = "sphere", position = position, body = "dynamic", shape = "sphere", mass = 2 })
    return e
  end

  -- Spin in degrees: set on the frame it is created, it reads back and turns
  local top = sphere("Top", vec3(0, 10, 0))
  top.angularVelocity = vec3(0, 60, 0)
  test.near(top.angularVelocity, vec3(0, 60, 0), 0.01, "reads back in degrees before the body is built")
  wait(0.5)
  test.near(top.rotation.y, 30, 2.0, "turned 30 degrees in half a second")

  -- An angular impulse spins at once: 1.6 N*m*s / 0.8 kg m^2 = 2 rad/s, about 114.6 degrees per second
  local kick = sphere("Kick", vec3(10, 10, 0))
  kick:angularImpulse(vec3(0, 0, 1.6))
  wait(1 / 60)
  test.near(kick.angularVelocity, vec3(0, 0, 114.6), 1.0, "an angular impulse on the frame it was made")

  -- A push at a point to the side moves the body and spins it; a torque spins up over steps
  local side = sphere("Side", vec3(20, 10, 0))
  side:impulseAt(vec3(0, 0, 4), vec3(21, 10, 0))
  local wheel = sphere("Wheel", vec3(30, 10, 0))
  wheel:forceAt(vec3(0, 0, 240), vec3(31, 10, 0)) -- for one step: 4 N*s on the side of it
  wait(1 / 60)
  test.near(side.velocity, vec3(0, 0, 2), 0.05, "pushed along z")
  test.near(side.angularVelocity, vec3(0, -286.5, 0), 2, "and spun about -y by the point it was pushed at")
  test.near(wheel.velocity, vec3(0, 0, 2), 0.05, "forceAt pushes the same way")

  -- A torque for each step: 8 N*m for 30 steps is 5 rad/s
  for _ = 1, 30 do
    wheel:torque(vec3(0, 0, 8))
    wait() -- one frame: one physics step, which the torque lasts
  end
  -- Its forceAt push also spun it about -y: only the z spin is the torque's. The default angular damping (0.05, not
  -- settable from Lua) takes about 1% of it over these steps, hence the wider tolerance
  test.near(wheel.angularVelocity.z, 286.5, 6, "torque spins it up, about 286 degrees per second")
end)
