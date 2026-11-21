-- Physics queries with options: rays that ignore entities or hit characters, raycastBoxes' ignore, and overlapSphere and
-- overlapBox with layers, ignore and characters

test.run(function()
  engine.create("Floor", { model = "box", position = vec3(0, -0.5, 0), scale = vec3(40, 1, 40), body = "static" })
  local hero = engine.create("Hero", { position = vec3(0, 0.9, 0), character = true })
  local near = engine.create("Near", { model = "box", position = vec3(-3, 1, 0), body = "static" })
  local far = engine.create("Far", { model = "box", position = vec3(-6, 1, 0), body = "static", layer = "enemy" })
  wait(1)

  -- Characters: a ray passes through them unless asked
  local from, toward = vec3(4, 0.9, 0), vec3(-1, 0, 0)
  local hit = physics.raycast(from, toward, 20)
  test.eq(hit.entity, near, "by default the ray goes through the character and hits the box behind it")
  hit = physics.raycast(from, toward, 20, { characters = true })
  test.eq(hit.entity, hero, "with characters = true it hits the character")
  test.near(hit.distance, 4 - 0.4, 0.2, "at the capsule's surface")
  hit = physics.raycast(from, toward, 20, { characters = true, ignore = hero })
  test.eq(hit.entity, near, "ignored, it is passed through again")

  -- Ignoring one entity or a list of them
  hit = physics.raycast(vec3(-1.5, 1, 0), vec3(-1, 0, 0), 20)
  test.eq(hit.entity, near)
  hit = physics.raycast(vec3(-1.5, 1, 0), vec3(-1, 0, 0), 20, { ignore = near })
  test.eq(hit.entity, far, "the next one behind")
  hit = physics.raycast(vec3(-1.5, 1, 0), vec3(-1, 0, 0), 20, { ignore = { near, far } })
  test.eq(hit, nil, "nothing left")
  hit = physics.raycast(vec3(-1.5, 1, 0), vec3(-1, 0, 0), 20, { ignore = { near }, layers = { "static" } })
  test.eq(hit, nil, "the layers and ignore work together")
  test.ok(not pcall(function() physics.raycast(from, toward, 20, { ignore = "near" }) end), "a name is not an entity")

  -- Collision boxes
  local boxA = engine.create("BoxA", { model = "box", position = vec3(0, 5, -4), collision = true })
  local boxB = engine.create("BoxB", { model = "box", position = vec3(0, 5, -8), collision = true })
  hit = physics.raycastBoxes(vec3(0, 5, 0), vec3(0, 0, -1), 30)
  test.eq(hit.entity, boxA)
  hit = physics.raycastBoxes(vec3(0, 5, 0), vec3(0, 0, -1), 30, { ignore = boxA })
  test.eq(hit.entity, boxB, "raycastBoxes skips an ignored entity")
  hit = physics.raycastBoxes(vec3(0, 5, 0), vec3(0, 0, -1), 30, { ignore = { boxA, boxB } })
  test.eq(hit, nil)

  -- Overlaps: the bodies in a sphere or a box, each once, characters only when asked
  local found = physics.overlapSphere(vec3(-3, 1, 0), 0.8)
  test.eq(#found, 1)
  test.eq(found[1], near)
  found = physics.overlapSphere(vec3(-4.5, 2.2, 0), 1.3)
  test.eq(#found, 2, "both boxes are within reach")
  found = physics.overlapSphere(vec3(-4.5, 2.2, 0), 1.3, { layers = { "enemy" } })
  test.eq(#found, 1)
  test.eq(found[1], far)
  found = physics.overlapSphere(vec3(-4.5, 2.2, 0), 1.3, { ignore = far })
  test.eq(#found, 1)
  test.eq(found[1], near)
  found = physics.overlapSphere(vec3(0, 0.9, 0), 1)
  for _, e in ipairs(found) do
    test.ok(e ~= hero, "the character is left out by default")
  end
  found = physics.overlapSphere(vec3(0, 0.9, 0), 1, { characters = true })
  local sawHero = false
  for _, e in ipairs(found) do
    sawHero = sawHero or e == hero
  end
  test.ok(sawHero, "characters = true brings it in")
  found = physics.overlapSphere(vec3(0, 0.9, 0), 1, { characters = true, ignore = hero })
  for _, e in ipairs(found) do
    test.ok(e ~= hero, "unless ignored")
  end
  test.eq(#physics.overlapSphere(vec3(0, 30, 0), 1), 0, "empty space")

  -- A box, turned: long in z it misses the crate beside it, turned 90 degrees about y it reaches
  local bar = engine.create("Bar", { model = "box", position = vec3(10, 1, 0), body = "static" })
  wait(0.2)
  test.eq(#physics.overlapBox(vec3(12.2, 1, 0), vec3(0.2, 0.2, 3)), 0, "long in z, beside the bar")
  found = physics.overlapBox(vec3(12.2, 1, 0), vec3(0.2, 0.2, 3), vec3(0, 90, 0))
  test.eq(#found, 1, "turned, it spans x and touches the bar")
  test.eq(found[1], bar)
  found = physics.overlapBox(vec3(10, 1, 0), vec3(0.1, 0.1, 0.1), nil, { ignore = bar })
  test.eq(#found, 0, "options without a rotation")
end)
