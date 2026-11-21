-- camera: lookAt, fov, the follow rig converging on a moving target, and a manual lookAt winning over the rig

test.run(function()
  -- lookAt places the active camera and aims it
  camera.lookAt(vec3(0, 5, 10), vec3(0, 0, 0))
  test.near(camera.position(), vec3(0, 5, 10))
  local expected = (vec3(0, 0, 0) - vec3(0, 5, 10)):normalized()
  test.near(camera.forward(), expected, 0.01, "looks at the target")
  camera.lookAt(vec3(-3, 1, 0), vec3(7, 1, 0))
  test.near(camera.forward(), vec3(1, 0, 0), 0.01)

  camera.setFov(70)
  test.near(camera.fov(), 70)
  wait(0.1)

  -- A rigid follow puts the camera where the offset says at once: behind the target (+Z for one facing -Z)
  local target = engine.create("Target", { model = "box", position = vec3(0, 0, 0) })
  test.ok(not camera.isFollowing())
  camera.follow(target, { offset = vec3(0, 4, 7) })
  test.ok(camera.isFollowing())
  wait()
  wait()
  test.near(camera.position(), vec3(0, 4, 7), 0.05, "behind and above the target")
  test.near(camera.forward().z, -0.87, 0.1, "looking down at it")
  target.position = vec3(10, 0, 0)
  wait()
  wait()
  test.near(camera.position(), vec3(10, 4, 7), 0.05, "a rigid rig keeps up exactly")

  -- With smoothing it trails, then catches up and settles
  camera.stopFollow()
  test.ok(not camera.isFollowing())
  camera.follow(target, { offset = vec3(0, 4, 7), smoothing = 0.3 })
  wait(1)
  test.near(camera.position(), vec3(10, 4, 7), 0.05, "settled on the target")
  target.position = vec3(20, 0, 0)
  wait(0.1)
  local lagging = camera.position()
  test.ok(lagging.x > 10.5 and lagging.x < 19.5, "on its way: " .. tostring(lagging))
  wait(0.5)
  local nearer = camera.position()
  test.ok(nearer.x > lagging.x and nearer.x < 20, "still closing in: " .. tostring(nearer))
  wait(3)
  test.near(camera.position(), vec3(20, 4, 7), 0.05, "converged")

  -- The offset turns with the target (followRotation): behind a target facing +X is at -X
  camera.stopFollow()
  target.rotation = vec3(0, -90, 0) -- forward is +X
  test.near(target.forward, vec3(1, 0, 0), 0.001)
  camera.follow(target, { offset = vec3(0, 4, 7) })
  wait()
  wait()
  test.near(camera.position(), vec3(20 - 7, 4, 0), 0.05)

  -- A lookAt in a frame wins over the rig for that frame; the rig then picks up from where the camera was left
  camera.stopFollow()
  target.rotation = vec3(0, 0, 0)
  camera.follow(target, { offset = vec3(0, 4, 7), smoothing = 0.2 })
  wait(2)
  test.near(camera.position(), vec3(20, 4, 7), 0.05)
  camera.lookAt(vec3(50, 5, 5), vec3(50, 0, 0))
  wait() -- the frame the lookAt was made in has gone by
  test.near(camera.position(), vec3(50, 5, 5), 0.01, "the manual camera won")
  test.ok(camera.isFollowing(), "the rig is still there")
  wait(3)
  test.near(camera.position(), vec3(20, 4, 7), 0.1, "and the rig took over again")

  -- A target that is destroyed leaves the camera alone and the game running
  local fallen = engine.create("Fallen", { position = vec3(0, 0, 0) })
  camera.follow(fallen, { offset = vec3(0, 1, 3) })
  wait()
  fallen:destroy()
  wait(0.3)
  camera.stopFollow()
  test.ok(not camera.isFollowing())

  -- A follow from code survives a scene load: the request goes on the new scene's camera, by target name
  local floor = engine.create("Floor", { position = vec3(0, 0, 0) })
  camera.follow(floor, { offset = vec3(0, 2, 5) })
  engine.loadScene("scenes/physics.json")
  wait()
  wait(0.5)
  test.eq(engine.isLoadingScene(), false, "the switch is done")
  test.ok(camera.isFollowing(), "still following after the scene load")
  local newFloor = engine.find("Floor")
  test.ok(newFloor ~= nil, "the new scene has its own Floor")
  wait()
  test.near(camera.position(), newFloor.position + vec3(0, 2, 5), 0.05, "behind the new scene's Floor")

  -- stopFollow forgets the request: the next load leaves the camera alone
  camera.stopFollow()
  engine.loadScene("scenes/physics.json")
  wait()
  wait(0.5)
  test.ok(not camera.isFollowing(), "no follow after stopFollow and a load")
end)
