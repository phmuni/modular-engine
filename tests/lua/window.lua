-- window: the window's size, the render scale and the scene's resolution. The framebuffer has `density` pixels per
-- window unit (2 on a Retina screen, 1 elsewhere), which the test measures instead of assuming.

test.run(function()
  wait() -- the first frame: the framebuffer's size is known

  -- The render scale: 1 by default, clamped to 0.5 to 2, and the scene's pixels follow it
  test.eq(engine.renderScale(), 1.0, "starts at 100 %")
  local width, height = engine.windowSize()
  local sceneWidth, sceneHeight = engine.renderResolution()
  local density = sceneWidth / width
  test.ok(density >= 1, "the framebuffer has at least a pixel per window unit")
  test.eq(sceneHeight, height * density, "at 100 % the scene has the framebuffer's pixels")
  local pixelsWide, pixelsHigh = sceneWidth, sceneHeight

  engine.setRenderScale(0.5)
  test.eq(engine.renderScale(), 0.5)
  sceneWidth, sceneHeight = engine.renderResolution()
  test.eq(sceneWidth, math.floor(pixelsWide * 0.5 + 0.5), "half the pixels across")
  test.eq(sceneHeight, math.floor(pixelsHigh * 0.5 + 0.5))

  engine.setRenderScale(2)
  sceneWidth, sceneHeight = engine.renderResolution()
  test.eq(sceneWidth, pixelsWide * 2, "supersampling")
  test.eq(sceneHeight, pixelsHigh * 2)

  engine.setRenderScale(0.1)
  test.eq(engine.renderScale(), 0.5, "clamped from below")
  engine.setRenderScale(10)
  test.eq(engine.renderScale(), 2.0, "clamped from above")
  wait() -- a frame drawn at the largest scale does not break anything

  -- The window's size: what was set is what is read, and the scene follows the new size
  engine.setRenderScale(1)
  engine.setWindowSize(640, 360)
  width, height = engine.windowSize()
  test.eq(width, 640, "setWindowSize round-trips")
  test.eq(height, 360)
  sceneWidth, sceneHeight = engine.renderResolution()
  test.eq(sceneWidth, 640 * density, "the scene is resized with the window")
  test.eq(sceneHeight, 360 * density)

  engine.setRenderScale(0.75)
  sceneWidth, sceneHeight = engine.renderResolution()
  test.eq(sceneWidth, 480 * density)
  test.eq(sceneHeight, 270 * density)
  wait()

  engine.setWindowSize(0, 100) -- not a size: nothing happens
  width, height = engine.windowSize()
  test.eq(width, 640)
  test.eq(height, 360)

  -- The sizes on offer: some, smallest first, and each one a real size
  local sizes = engine.resolutions()
  test.ok(#sizes > 0, "there is always a size to choose")
  for index, size in ipairs(sizes) do
    test.ok(size.width > 0 and size.height > 0, "a real size")
    if index > 1 then
      test.ok(size.width >= sizes[index - 1].width, "smallest first")
    end
  end

  test.ok(not engine.isFullscreen(), "tests run in a window")
end)
