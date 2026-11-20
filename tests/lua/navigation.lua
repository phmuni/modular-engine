-- nav: a path is found around an obstacle, none through a closed room, and walkable() tells ground from walls

test.run(function()
  -- A 30 x 30 floor (top at y = 0) with a wall across the middle: x in [-0.5, 0.5], z in [-5, 5]
  engine.create("Floor", { model = "box", position = vec3(0, -0.5, 0), scale = vec3(30, 1, 30), body = "static" })
  engine.create("Wall", { model = "box", position = vec3(0, 1, 0), scale = vec3(1, 2, 10), body = "static" })
  -- A closed room on the right: four walls around (10, 0, 10)
  for _, wall in ipairs({
    { vec3(10, 1, 7), vec3(6, 2, 0.5) }, { vec3(10, 1, 13), vec3(6, 2, 0.5) },
    { vec3(7, 1, 10), vec3(0.5, 2, 6) }, { vec3(13, 1, 10), vec3(0.5, 2, 6) },
  }) do
    engine.create("RoomWall", { model = "box", position = wall[1], scale = wall[2], body = "static" })
  end
  wait() -- the bodies join the physics world

  test.ok(nav.path(vec3(-5, 0, 0), vec3(5, 0, 0)) == nil, "no map built yet: no path")
  test.ok(nav.build({ min = vec3(-14, -1, -14), max = vec3(14, 3, 14), cellSize = 0.5, agentRadius = 0.3 }), "the map is built")

  -- Across the wall: the straight line is blocked, so the path bends round an end of it
  local from, to = vec3(-5, 0, 0), vec3(5, 0, 0)
  local path = nav.path(from, to)
  test.ok(path ~= nil and #path >= 3, "a path around the wall")
  if path then
    test.near(path[1].x, from.x, 1, "starts at the start")
    test.near(path[#path], to, 0.7, "ends at the goal")
    local length, goesAround = 0, false
    for i = 2, #path do
      length = length + (path[i] - path[i - 1]):length()
    end
    for _, point in ipairs(path) do
      if math.abs(point.z) >= 5 then goesAround = true end
      test.ok(not (math.abs(point.x) < 0.5 and math.abs(point.z) < 5), "no point inside the wall: " .. tostring(point))
    end
    test.ok(goesAround, "goes past an end of the wall (z beyond 5)")
    test.ok(length > 12 and length < 30, "longer than the straight 10 m, and not absurd: " .. length)
  end

  -- With nothing in the way the path is direct
  local direct = nav.path(vec3(-10, 0, -10), vec3(-10, 0, 10))
  test.ok(direct ~= nil and #direct <= 3, "a straight line needs few points")

  -- Into the closed room: no way in
  test.ok(nav.path(vec3(-5, 0, -5), vec3(10, 0, 10)) == nil, "no path into a closed room")

  -- Ground, and what is off the map
  test.ok(nav.walkable(vec3(-5, 0, 0)), "open floor")
  test.ok(nav.walkable(vec3(10, 0, 10)), "inside the closed room: ground, only cut off from the rest")
  test.ok(not nav.walkable(vec3(100, 0, 100)), "off the map")
end)
