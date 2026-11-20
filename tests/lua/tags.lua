-- Tags: add, remove, has, each kept once, the empty one ignored, the Tags component going with the last tag

test.run(function()
  local e = engine.create("Tagged", { model = "box" })
  test.eq(#e:tags(), 0)
  test.ok(not e:has("Tags"), "no Tags component until the first tag")
  test.ok(not e:hasTag("enemy"))

  e:addTag("enemy")
  test.ok(e:hasTag("enemy"))
  test.ok(e:has("Tags"))
  e:addTag("enemy")
  test.eq(#e:tags(), 1, "a tag is kept once")
  e:addTag("flying")
  e:addTag("")
  test.eq(#e:tags(), 2, "an empty tag is ignored")
  test.eq(e:tags(), { "enemy", "flying" }, "in the order added")

  test.eq(#engine.findTagged("enemy"), 1)
  test.eq(#engine.findTagged("flying"), 1)

  e:removeTag("enemy")
  test.ok(not e:hasTag("enemy"))
  test.ok(e:hasTag("flying"))
  test.eq(#engine.findTagged("enemy"), 0)
  e:removeTag("nothing") -- not there: nothing happens
  test.eq(#e:tags(), 1)

  e:removeTag("flying")
  test.eq(#e:tags(), 0)
  test.ok(not e:has("Tags"), "the last tag takes the component with it")
  test.eq(#engine.findWithComponent("Tags"), 0)

  -- Tags are per entity
  local other = engine.create("Other", { model = "box", tags = { "enemy" } })
  e:addTag("enemy")
  other:removeTag("enemy")
  test.ok(e:hasTag("enemy"))
  test.ok(not other:hasTag("enemy"))
  test.eq(engine.findTagged("enemy")[1], e)
end)
