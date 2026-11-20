-- input from Lua with nothing pressed: the idle values, the text entry switches and the mouse switches. Events pushed
-- through SDL (typing, mouse motion) are in the C++ tests input_text and input_mouse.

test.run(function()
  wait()
  test.eq(input.key("Space"), false)
  test.eq(input.keyPressed("Space"), false)
  test.eq(input.down("jump"), false, "an action nothing binds is not down")
  test.eq(input.axis("moveX"), 0)
  test.eq(input.click(), false)
  test.eq(input.mouseDown(3), false)
  test.eq(input.wheel(), 0)
  local dx, dy = input.mouseDelta()
  test.eq(dx, 0)
  test.eq(dy, 0)
  local mx, my = input.mouse()
  test.ok(type(mx) == "number" and type(my) == "number")

  -- Text entry
  test.eq(input.isTextActive(), false)
  input.startText("ab", 5)
  test.eq(input.isTextActive(), true)
  test.eq(input.text(), "ab", "starts from the text given")
  input.setText("abc")
  test.eq(input.text(), "abc")
  wait()
  test.eq(input.typed(), "", "nothing typed this frame")
  input.stopText()
  test.eq(input.isTextActive(), false)

  -- Mouse capture and cursor
  input.captureMouse(true)
  test.eq(input.isMouseCaptured(), true)
  input.captureMouse(false)
  test.eq(input.isMouseCaptured(), false)
  input.showCursor(false)
  test.eq(input.isCursorVisible(), false)
  input.showCursor(true)
  test.eq(input.isCursorVisible(), true)

  -- No gamepad is plugged in: rumble says so
  test.eq(input.rumble(1, 1, 0.1), false)
  input.stopRumble()
end)
