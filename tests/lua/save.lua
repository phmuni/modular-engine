-- save: set, get, has, remove and write from Lua. Tests run with the save data in memory only, so the real save folder is
-- never touched (the C++ test save_to_folder checks the file itself, in a temporary folder).

test.run(function()
  test.eq(save.get("score", 0), 0, "the fallback when there is nothing")
  test.eq(save.get("score"), nil)
  test.ok(not save.has("score"))

  save.set("score", 120)
  save.set("name", "Ada")
  save.set("muted", true)
  save.set("inventory", { "key", "map" })
  save.set("stats", { count = 2, nested = { depth = 1, tags = { "a", "b" } } })
  test.eq(save.get("score"), 120)
  test.eq(save.get("name"), "Ada")
  test.eq(save.get("muted"), true)
  test.ok(save.has("score"))
  local inventory = save.get("inventory")
  test.eq(inventory[1], "key")
  test.eq(inventory[2], "map")
  test.eq(#inventory, 2)
  local stats = save.get("stats")
  test.eq(stats.count, 2)
  test.eq(stats.nested.depth, 1)
  test.eq(stats.nested.tags, { "a", "b" })

  save.set("score", 200)
  test.eq(save.get("score"), 200, "set replaces")
  save.set("score", "many")
  test.eq(save.get("score"), "many", "of any kind")

  save.remove("score")
  test.ok(not save.has("score"))
  test.eq(save.get("score", -1), -1)
  save.remove("never there") -- nothing happens

  test.ok(save.write(), "write succeeds (and does nothing, in memory)")

  -- What cannot be saved is reported (as an error to the script), not stored
  test.expectError("only numbers, strings, booleans and tables")
  save.set("function", function() end)
  test.ok(not save.has("function"))
  wait()
end)
