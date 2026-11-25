-- Animation layers from Lua: entity:animationLayerState(layer), and the layer name that onStateChanged gets last

local MODEL = "models/knight/knight.glb"

test.run(function()
  game.changes = {}
  local e = engine.create("Knight", { model = MODEL, script = "../tests/fixtures/state_listener.lua" })
  test.eq(e:animationLayerState("upper"), "", "no machine yet")
  e:setAnimator("../tests/fixtures/animators/knight_upper.json")
  test.eq(e.animationState, "walk")
  test.eq(e:animationLayerState("upper"), "none", "a layer starts in its default state")
  test.eq(e:animationLayerState("base"), "walk", "base is the top-level states")
  test.eq(e:animationLayerState(""), "walk")
  test.eq(e:animationLayerState("nothing"), "", "a layer the file does not have")

  e:trigger("attack")
  wait(0.1)
  test.eq(e:animationLayerState("upper"), "attack")
  test.eq(e.animationState, "walk", "the base goes on")
  test.eq(game.changes[1], "none>attack@upper", "onStateChanged(old, new, layer)")

  -- The swing ends by itself (a second long, then the fade)
  wait(1.4)
  test.eq(e:animationLayerState("upper"), "none")
  test.eq(game.changes[2], "attack>none@upper")
  test.eq(#game.changes, 2, "the base did not change")
end)
