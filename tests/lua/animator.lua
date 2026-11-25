-- Animation state machines from Lua: the default state, parameters and transitions (float, trigger, exit time, any
-- state), the onStateChanged callback, a manual play, a blend, and files that do not load

local MACHINE = "../tests/fixtures/animators/machine.json"
local MODEL = "models/tentacle/tentacle.glb"

test.run(function()
  game.changes = {}
  local e = engine.create("Tentacle", { model = MODEL, script = "../tests/fixtures/state_listener.lua" })
  test.eq(e.animationState, "", "no machine yet")
  e:setAnimator(MACHINE)
  test.ok(e:has("AnimationStateMachine"))
  test.ok(e:has("Animator"), "setAnimator adds the Animator it drives")
  test.eq(e.animationState, "idle", "the default state at once")
  test.eq(e:param("grounded"), 1, "a bool's default from the file")
  wait(0.2)
  test.eq(e.animationState, "idle", "stays while no transition holds")
  test.eq(#game.changes, 0, "entering the default state is not a change")

  -- A float condition
  e:setParam("speed", 1.0)
  test.near(e:param("speed"), 1.0)
  wait(0.1)
  test.eq(e.animationState, "walk")
  e:setParam("speed", 0)
  wait(0.1)
  test.eq(e.animationState, "idle")
  test.eq(game.changes[1], "idle>walk@base", "onStateChanged(old, new, layer)")
  test.eq(game.changes[2], "walk>idle@base")

  -- A trigger fires one transition and is used up
  e:trigger("poke")
  test.eq(e:param("poke"), 1)
  wait(0.1)
  test.eq(e.animationState, "poked")
  test.eq(e:param("poke"), 0, "used up by the transition")
  -- exitTime 0.5 of a 2 s clip: back to idle after a second, not before
  wait(0.5)
  test.eq(e.animationState, "poked", "before the exit time")
  wait(0.7)
  test.eq(e.animationState, "idle", "after the exit time")

  -- From any state
  e:setParam("speed", 1)
  wait(0.1)
  test.eq(e.animationState, "walk")
  e:trigger("hit")
  wait(0.1)
  test.eq(e.animationState, "hurt", "any state goes to hurt")
  e:setParam("speed", 0)
  wait(2.2)
  test.eq(e.animationState, "idle", "a transition with no condition waits for the end of the clip")

  -- A manual play takes the Animator over, and the machine goes on
  e:play("Twist")
  e:trigger("poke")
  wait(0.1)
  test.eq(e.animationState, "poked", "the machine still moves while a clip is played by hand")

  -- An undeclared parameter is a warning; its value is not kept
  e:setParam("nothing", 3)
  test.eq(e:param("nothing"), 0)

  -- A file that is missing, or does not parse, or names a state that is not there, does nothing
  local bad = engine.create("Bad", { model = MODEL })
  bad:setAnimator("../tests/fixtures/animators/no_such.json")
  bad:setAnimator("../tests/fixtures/animators/bad.json")
  bad:setAnimator("../tests/fixtures/animators/unknown_state.json")
  wait(0.1)
  test.eq(bad.animationState, "")
  test.ok(bad.valid, "still there")
  bad:setAnimator("")
  test.ok(not bad:has("AnimationStateMachine"), "an empty path removes the machine")

  -- A blend: runs, and moves along with its parameter
  local blended = engine.create("Blended", { model = MODEL })
  blended:setAnimator("../tests/fixtures/animators/blend.json")
  test.eq(blended.animationState, "move")
  blended:setParam("speed", 2)
  wait(0.2)
  test.eq(blended.animationState, "move")
  test.near(blended:param("speed"), 2)

  -- Parameters fed by a character controller: its ground contact and speed
  engine.create("Floor", { model = "box", position = vec3(0, -0.5, 0), scale = vec3(30, 1, 30), body = "static" })
  local hero = engine.create("Hero", { model = MODEL, position = vec3(0, 1.2, 0), character = {} })
  hero:setAnimator("../tests/fixtures/animators/character.json")
  wait(1.0)
  test.ok(hero.onGround, "standing on the floor")
  test.eq(hero.animationState, "idle")
  hero:move(vec3(3, 0, 0))
  wait(0.3)
  test.eq(hero.animationState, "walk", "speed comes from the character")
  test.ok(hero:param("speed") > 0.5)
  hero:move(vec3(0, 0, 0))
  wait(0.3)
  test.eq(hero.animationState, "idle")
  hero:jump(6)
  wait(0.2)
  test.eq(hero.animationState, "air", "off the ground")
  wait(2)
  test.eq(hero.animationState, "idle", "landed")
end)
