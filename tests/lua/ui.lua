-- ui from Lua: the helpers on a document that is there and on ids that are not must not crash. What they change in the
-- document is checked from C++ (the C++ test ui_helpers); Lua has no way to read an element back.

test.run(function()
  test.ok(ui.load("../tests/fixtures/ui/test.rml"), "the document loads")
  test.ok(ui.load("../tests/fixtures/ui/test.rml"), "loading it again just shows it")
  test.ok(not ui.load("../tests/fixtures/ui/no_such_document.rml"), "a missing document is false, not an error")

  ui.text("score", 42)
  ui.text("score", 1.5)
  ui.text("banner", "Level <b>2</b>")
  ui.text("score", true)
  ui.show("banner", false)
  ui.show("banner", true)
  ui.class("banner", "hot", true)
  ui.class("banner", "hot", false)
  ui.style("banner", "color", "#ff0000")
  ui.style("banner", "color", "")
  ui.attribute("banner", "title", "tip")
  local clicks = 0
  test.ok(ui.onClick("button", function() clicks = clicks + 1 end), "a click handler on a button that exists")
  test.ok(not ui.onClick("no_such_button", function() end), "and none on one that does not")

  -- Ids that are not in any document are ignored
  ui.text("nothing", "x")
  ui.show("nothing", false)
  ui.class("nothing", "hot", true)
  ui.style("nothing", "color", "red")
  ui.attribute("nothing", "title", "x")

  wait(0.2) -- a few frames drawn with it
  test.ok(not ui.pauseMenu():isOpen(), "the game is not paused")
  test.eq(clicks, 0)
end)
