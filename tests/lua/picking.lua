-- picking: the ray through a point of the window, the model under it, ray casts against models and Collision boxes,
-- and the Collision box query between two entities

test.run(function()
  camera.lookAt(vec3(0, 0, 10), vec3(0, 0, 0))
  -- A box (1 unit wide, its front face at z = 0.5) in front of the camera, at the screen's center, and one behind it
  local target = engine.create("Target", { model = "box", position = vec3(0, 0, 0), collision = true })
  local behind = engine.create("Behind", { model = "box", position = vec3(0, 0, -20), collision = true })
  wait()
  wait()
  -- The window's size once it is shown (a window system may apply a resize only then), as screenRay sees it
  local width, height = engine.windowSize()
  local cx, cy = width / 2, height / 2

  -- screenRay: from the camera, through the center of the window, straight ahead
  local origin, direction = engine.screenRay(cx, cy)
  test.ok(origin ~= nil and direction ~= nil, "the active camera gives a ray")
  test.near(origin, vec3(0, 0, 10), 0.2, "starts at the camera (on its near plane)")
  test.near(direction, vec3(0, 0, -1), 0.01, "the center looks straight ahead")
  test.near(direction:length(), 1, 0.001, "unit length")
  local _, rightward = engine.screenRay(width * 0.9, cy)
  test.ok(rightward.x > 0.05 and math.abs(rightward.y) < 0.01, "right of the center looks to the right: " .. tostring(rightward))
  local _, upward = engine.screenRay(cx, height * 0.1)
  test.ok(upward.y > 0.05 and math.abs(upward.x) < 0.01, "above the center looks up: " .. tostring(upward))

  -- pick: the nearest model under the point, with the surface it hit
  local hit = engine.pick(cx, cy)
  test.ok(hit ~= nil, "the box is under the center")
  test.eq(hit.entity, target, "the nearest one, not the box behind it")
  test.near(hit.point, vec3(0, 0, 0.5), 0.01, "its front face")
  test.near(hit.normal, vec3(0, 0, 1), 0.01, "facing the camera")
  test.near(hit.distance, 9.5, 0.2, "from the camera")
  test.ok(engine.pick(width * 0.95, height * 0.05) == nil, "nothing in the corner")
  local picked = engine.pick(input.mouse()) -- the mouse position goes straight in
  test.ok(picked == nil or picked.entity ~= nil, "the mouse position is a valid argument")

  -- Ray casts against models and Collision boxes
  local model = physics.raycastModels(vec3(0, 0, 10), vec3(0, 0, -1))
  test.eq(model.entity, target)
  test.near(model.distance, 9.5, 0.01)
  test.ok(physics.raycastModels(vec3(0, 0, 10), vec3(0, 0, -1), 5) == nil, "out of reach")
  test.ok(physics.raycastModels(vec3(0, 0, 10), vec3(0, 1, 0)) == nil, "nothing above")
  local box = physics.raycastBoxes(vec3(0, 0, 10), vec3(0, 0, -1), 50)
  test.eq(box.entity, target, "the box in front")
  test.near(box.distance, 9.5, 0.01)
  test.ok(physics.raycastBoxes(vec3(0, 0, 10), vec3(0, 0, -1), 5) == nil)

  -- isTouching: the Collision boxes of two entities overlap or do not
  local other = engine.create("Other", { model = "box", position = vec3(0.5, 0, 0), collision = true })
  wait()
  test.eq(target:isTouching(other), true, "overlapping by half a box")
  test.eq(other:isTouching(target), true, "either way")
  test.eq(target:isTouching(behind), false, "far apart")
  other.position = vec3(5, 0, 0)
  test.eq(target:isTouching(other), false, "moved away: asked again, answered again")
  target:fitCollision(2)
  test.eq(target:isTouching(behind), false, "a bigger box still does not reach")
  test.eq(target:isTouching(nil), false, "nothing is not touched")
  other:destroy()
  wait()
  test.eq(target:isTouching(other), false, "a destroyed entity is not touched")

  -- The part argument is accepted (by number) and the whole-model form still works
  target:setColor(vec3(1, 0, 0), 0)
  target:setEmission(vec3(1, 0, 0), 2, 0)
  target:setRoughness(0.5, 0)
  target:setColor(vec3(0, 1, 0))
  test.near(target:color(), vec3(0, 1, 0), 0.001)
end)
