-- Entity handle properties and methods: position, rotation, scale, forward, right, colors, visibility, destroy

test.run(function()
  local e = engine.create("Thing", { model = "box", position = vec3(1, 2, 3) })
  test.ok(e.valid, "valid after create")
  test.eq(e.name, "Thing")
  test.near(e.position, vec3(1, 2, 3))
  test.near(e.scale, vec3(1, 1, 1))

  e.position = vec3(-4, 0.5, 9)
  test.near(e.position, vec3(-4, 0.5, 9))
  test.near(e.position.y, 0.5)
  e.scale = vec3(2, 3, 4)
  test.near(e.scale, vec3(2, 3, 4))
  e.name = "Renamed"
  test.eq(e.name, "Renamed")
  test.eq(engine.find("Renamed"), e, "found by its new name")

  -- forward is -Z turned by the yaw (rotation.y); right is to its right
  e.rotation = vec3(0, 0, 0)
  test.near(e.forward, vec3(0, 0, -1))
  test.near(e.right, vec3(1, 0, 0))
  e.rotation = vec3(0, 90, 0)
  test.near(e.rotation, vec3(0, 90, 0), 0.001)
  test.near(e.forward, vec3(-1, 0, 0), 0.001)
  test.near(e.right, vec3(0, 0, -1), 0.001)
  test.near(e.forward:length(), 1)
  e.rotation = vec3(0, 180, 0)
  test.near(e.forward, vec3(0, 0, 1), 0.001)

  -- Color and visibility
  e:setColor(vec3(1, 0, 0.5))
  test.near(e:color(), vec3(1, 0, 0.5))
  test.ok(e.visible)
  e:setVisible(false)
  test.eq(e.visible, false)
  test.ok(e:has("Hidden"), "hiding adds the Hidden component")
  e:setVisible(true)
  test.ok(e.visible)
  e:setEmission(vec3(0, 1, 0), 2)
  test.ok(e:has("Model"))
  test.ok(not e:has("RigidBody"))
  test.ok(e.script == nil, "no script")

  -- The script's own fields live on the entity, not on the handle
  e.speed = 5
  test.eq(e.speed, 5)
  test.eq(engine.find("Renamed").speed, 5, "fields are per entity")

  -- Destroyed: valid says so, reads give defaults, writes do nothing, finders do not see it
  local alias = engine.find("Renamed")
  e:destroy()
  wait()
  test.eq(e.valid, false)
  test.eq(alias.valid, false, "every handle to it")
  test.ok(engine.find("Renamed") == nil or not engine.find("Renamed").valid, "not found by name")
  test.near(alias.position, vec3(0, 0, 0))
  alias.position = vec3(7, 7, 7)
  test.eq(alias.valid, false, "writing does not revive it")
  test.eq(#alias:tags(), 0)
end)
