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

  -- The top of a tall box has no step leading up to it: not walkable, and no path ends on it. A low step still is.
  engine.create("Tall", { model = "box", position = vec3(-8, 1, -8), scale = vec3(3, 2, 3), body = "static" })
  engine.create("Step", { model = "box", position = vec3(-8, 0.1, 8), scale = vec3(3, 0.2, 3), body = "static" })
  wait()
  test.ok(nav.build({ min = vec3(-14, -1, -14), max = vec3(14, 3, 14), cellSize = 0.5, agentRadius = 0.3 }), "rebuilt")
  test.ok(not nav.walkable(vec3(-8, 2, -8)), "the top of a tall box is not walkable")
  test.ok(nav.walkable(vec3(-8, 0.2, 8)), "a low step is")
  test.ok(nav.walkable(vec3(-5, 0, 0)), "open floor still is")
  test.ok(nav.path(vec3(-12, 0, -12), vec3(-8, 2, -8)) == nil, "no path to the top: no ground of the map is near it")
  local overStep = nav.path(vec3(-12, 0, 12), vec3(-4, 0, 12))
  test.ok(overStep ~= nil, "a path over the low step")

  -- nav.nearest: the point itself on the ground, else the nearest ground within the distance, else nil
  local here = nav.nearest(vec3(-5, 0.2, 0))
  test.ok(here ~= nil and math.abs(here.x + 5) < 0.01 and math.abs(here.y) < 0.15, "nearest: on the ground already")
  local beside = nav.nearest(vec3(0, 0, 0)) -- inside the wall
  test.ok(beside ~= nil and math.abs(beside.x) >= 0.5, "nearest: moved out of the wall: " .. tostring(beside))
  test.ok(nav.nearest(vec3(0, 0, 0), 0.1) == nil, "nearest: nothing within a tenth of a unit")
  test.ok(nav.nearest(vec3(100, 0, 100)) == nil, "nearest: off the map")

  -- A bridge: a deck above the floor, reached by a ramp; the point's height says which level it is on
  engine.create("Deck", { model = "box", position = vec3(-6, 3.35, -10), scale = vec3(6, 0.3, 3), body = "static" })
  local ramp = engine.create("RampSlab", { model = "box", position = vec3(-10.99, 1.635, -10), scale = vec3(5.5, 0.3, 3), body = "static" })
  ramp.rotation = vec3(0, 0, 40)
  wait()
  test.ok(nav.build({ min = vec3(-14, -1, -14), max = vec3(14, 6, 14), minRegionArea = 0 }), "built with the bridge")
  local deck = nav.nearest(vec3(-5, 3.9, -10))
  test.ok(deck ~= nil and math.abs(deck.y - 3.5) < 0.2, "a character on the deck is on the deck: " .. tostring(deck))
  local under = nav.nearest(vec3(-5, 0.9, -10))
  test.ok(under ~= nil and math.abs(under.y) < 0.2, "a character under it is on the floor: " .. tostring(under))
  local up = nav.path(vec3(-14, 0, -10), vec3(-5, 3.5, -10))
  test.ok(up ~= nil and up[#up].y > 3.2, "a path up to the deck")

  -- nav.rebuild: a wall that goes opens the way; only the tiles it touches are made again
  engine.create("Fence", { model = "box", position = vec3(10, 1, 4), scale = vec3(0.5, 2, 4), body = "static" })
  local gate = engine.create("Gate", { model = "box", position = vec3(10, 1, 0), scale = vec3(0.5, 2, 4), body = "static" })
  wait()
  test.ok(nav.rebuild(vec3(8, -1, -6), vec3(12, 4, 6)), "rebuild reports it did")
  test.ok(not nav.walkable(vec3(10, 0, 0)), "the gate is a wall")
  test.ok(not nav.rebuild(vec3(300, 0, 300), vec3(310, 5, 310)), "rebuild outside the map does nothing")
  gate:destroy()
  wait()
  test.ok(nav.walkable(vec3(-5, 0, 0)), "the rest of the map is as it was")
  test.ok(nav.rebuild(vec3(8, -1, -2), vec3(12, 4, 2)), "rebuilt after the gate went")
  test.ok(nav.walkable(vec3(10, 0, 0)), "the gate's place is ground again")
  test.ok(not nav.walkable(vec3(10, 0, 4)), "the fence still stands")
end)
