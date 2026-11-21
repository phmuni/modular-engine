-- engine.create with its options: model shorthands, color, body, character, collision, tags, components

test.run(function()
  for _, shape in ipairs({ "box", "sphere", "cylinder" }) do
    local e = engine.create(shape, { model = shape })
    test.ok(e.valid and e:has("Model"), shape .. " shorthand has a model")
  end

  local plain = engine.create("Plain")
  test.ok(plain.valid, "no options at all")
  test.ok(plain:has("Transform"))
  test.ok(not plain:has("Model"), "no model: only a Transform")
  test.near(plain.position, vec3(0, 0, 0))

  local placed = engine.create("Placed", {
    model = "box", position = vec3(1, 2, 3), rotation = vec3(0, 45, 0), scale = vec3(2, 2, 2),
  })
  test.near(placed.position, vec3(1, 2, 3))
  test.near(placed.rotation, vec3(0, 45, 0), 0.001)
  test.near(placed.scale, vec3(2, 2, 2))

  local red = engine.create("Red", { model = "box", color = vec3(1, 0, 0) })
  test.near(red:color(), vec3(1, 0, 0))

  -- Bodies: static and dynamic, and the shape and mass given
  local floor = engine.create("Floor", { model = "box", scale = vec3(10, 1, 10), body = "static" })
  local ball = engine.create("Ball", { model = "sphere", position = vec3(0, 5, 0), body = "dynamic", shape = "sphere", mass = 3 })
  local mover = engine.create("Mover", { model = "box", body = "kinematic" })
  test.ok(floor:has("RigidBody") and ball:has("RigidBody") and mover:has("RigidBody"))

  -- Collision boxes hug the model as placed, so two overlapping ones are found and two apart are not
  local a = engine.create("A", { model = "box", position = vec3(20, 0, 0), collision = true })
  test.ok(a:has("Collision"))
  test.ok(not floor:has("Collision"), "only when asked for")

  -- A character is its own body
  local hero = engine.create("Hero", { position = vec3(0, 2, 0), character = { radius = 0.4, height = 1.7 } })
  test.ok(hero:has("CharacterController"))
  test.ok(not hero:has("RigidBody"), "a character is not also a rigid body")
  local plainHero = engine.create("Hero2", { character = true })
  test.ok(plainHero:has("CharacterController"), "character = true takes the defaults")

  -- Tags
  local tagged = engine.create("Tagged", { model = "box", tags = { "enemy", "boss" } })
  test.ok(tagged:has("Tags"))
  test.ok(tagged:hasTag("enemy") and tagged:hasTag("boss"))
  test.ok(not tagged:hasTag("friend"))
  test.eq(#tagged:tags(), 2)
  test.eq(#engine.findTagged("enemy"), 1)

  -- A model that is not there is not fatal: the entity exists, with a warning in the log
  local missing = engine.create("Missing", { model = "no/such/model.obj" })
  test.ok(missing.valid, "a missing model leaves an entity")

  -- An unknown body name does not crash the engine either
  local odd = engine.create("Odd", { body = "whatever" })
  test.ok(odd.valid)
end)
