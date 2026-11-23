-- PBR materials from Lua: entity:setMetallic / setRoughness, on every part, a part by number and by name, and the
-- texture slots of the metallic-roughness and occlusion maps (the values themselves are checked by cpp/pbr_*)

test.run(function()
  local box = engine.create("Box", { model = "box" })
  box:setMetallic(1)
  box:setRoughness(0.2)
  box:setMetallic(0.5, 0)
  box:setRoughness(0.7, 0)
  box:setRoughness(0.7, "NoSuchPart") -- a warning, nothing set
  box:setTexture("metallicRoughness", "textures/bumps_normal.png")
  box:setTexture("occlusion", "textures/bumps_normal.png", 0)
  wait()
  test.ok(box.valid, "still running")
end)
