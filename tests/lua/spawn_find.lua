-- engine.spawn from a prefab (position, rotation, tags and children kept) and the finders: find, findAll, findTagged,
-- findWithComponent

local TURRET = "../tests/fixtures/turret.json"

test.run(function()
  local a = engine.spawn(TURRET, vec3(10, 0, 0))
  test.ok(a ~= nil and a.valid, "spawn returns the root")
  test.eq(a.name, "Turret")
  test.near(a.position, vec3(10, 0, 0))
  test.ok(a:hasTag("enemy") and a:hasTag("turret"), "the prefab's tags are kept")
  test.eq(#a:tags(), 2)

  -- Its child came along, and follows the root
  local children = a:children()
  test.eq(#children, 1)
  test.eq(children[1].name, "Barrel")
  test.eq(children[1]:parent(), a)
  wait()
  test.near(children[1].position, vec3(10, 1, 0), 0.01)

  -- Each spawn is a new copy, placed and turned as asked
  local b = engine.spawn(TURRET, vec3(-5, 0, 3), vec3(0, 90, 0))
  test.ok(b.valid and b ~= a)
  test.near(b.position, vec3(-5, 0, 3))
  test.near(b.rotation.y, 90, 0.01)
  test.eq(#engine.findAll("Turret"), 2)
  test.eq(#engine.findAll("Barrel"), 2)

  -- A prefab that is not there gives nothing, and the game goes on
  local nothing = engine.spawn("../tests/fixtures/no_such_prefab.json")
  test.ok(nothing == nil or not nothing.valid, "a missing prefab spawns nothing")

  -- find: the first by name, nothing when there is none
  test.eq(engine.find("Turret"), a)
  test.ok(engine.find("Ghost") == nil or not engine.find("Ghost").valid)
  local list = engine.findAll("Turret")
  test.eq(list[1], a, "by entity number")
  test.eq(list[2], b)
  test.eq(#engine.findAll("Ghost"), 0)

  -- findTagged looks only at the tagged
  engine.create("Plain", { model = "box" })
  local friendly = engine.create("Friendly", { model = "box", tags = { "friend" } })
  test.eq(#engine.findTagged("enemy"), 2)
  test.eq(#engine.findTagged("turret"), 2)
  test.eq(#engine.findTagged("friend"), 1)
  test.eq(engine.findTagged("friend")[1], friendly)
  test.eq(#engine.findTagged("nobody"), 0)

  -- findWithComponent: engine components by name
  test.eq(#engine.findWithComponent("Tags"), 3)
  test.eq(#engine.findWithComponent("Model"), 6)
  test.eq(#engine.findWithComponent("RigidBody"), 0)
  test.eq(#engine.findWithComponent("Camera"), 1, "the test's own camera")

  -- Destroying removes from every finder
  b:destroy()
  wait()
  test.eq(#engine.findAll("Turret"), 1)
  test.eq(#engine.findTagged("enemy"), 1)
  test.eq(#engine.findWithComponent("Tags"), 2)
  engine.destroy(a)
  wait()
  test.eq(#engine.findTagged("turret"), 0)
end)
