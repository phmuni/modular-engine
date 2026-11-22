-- Unlit parts from Lua: entity:setUnlit on every part, a part by number and by name (the pixels: cpp/unlit_*)

test.run(function()
  local box = engine.create("Box", { model = "box" })
  box:setUnlit(true)
  box:setUnlit(false, 0)
  box:setUnlit(true, 0)
  box:setUnlit(true, "NoSuchPart") -- a warning, nothing set
  wait()
  test.ok(box.valid, "still running")
end)
