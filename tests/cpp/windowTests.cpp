// The window's size, the render scale and the sizes on offer, through the C++ API (the Lua side is tests/lua/window.lua).

#include "foundation/core/engine.h"
#include "systems/windowSystem.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <cmath>

TEST_CASE(window_resolutions_fit_the_display) {
  const std::vector<glm::ivec2> sizes = engine.resolutions();
  CHECK(!sizes.empty());
  SDL_Window *window = engine.getSystemManager().getSystem<WindowSystem>().getWindow();
  SDL_Rect usable;
  CHECK(SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable));
  bool hasDesktop = false;
  for (size_t i = 0; i < sizes.size(); ++i) {
    CHECK(sizes[i].x > 0 && sizes[i].y > 0);
    CHECK(sizes[i].x <= usable.w && sizes[i].y <= usable.h);
    if (i > 0)
      CHECK(sizes[i].x >= sizes[i - 1].x);
    hasDesktop = hasDesktop || (sizes[i].x == usable.w && sizes[i].y == usable.h);
  }
  CHECK(hasDesktop); // the display's own size is always there
}

TEST_CASE(window_size_and_render_scale) {
  // Applying the largest size that is offered must work: every entry is a size the display can take
  const std::vector<glm::ivec2> sizes = engine.resolutions();
  engine.setWindowSize(sizes.front().x, sizes.front().y);
  CHECK_EQ(engine.windowSize().x, sizes.front().x);
  CHECK_EQ(engine.windowSize().y, sizes.front().y);

  engine.setWindowSize(-5, 100); // not a size
  CHECK_EQ(engine.windowSize().x, sizes.front().x);

  // The framebuffer has `density` pixels per window unit (2 on a Retina screen)
  const glm::ivec2 pixels = engine.renderResolution();
  CHECK(pixels.x >= sizes.front().x);
  engine.setRenderScale(0.5f);
  CHECK_NEAR(engine.renderScale(), 0.5f, 1e-6f);
  CHECK_EQ(engine.renderResolution().x, static_cast<int>(std::lround(pixels.x * 0.5)));
  engine.setRenderScale(100.0f);
  CHECK_NEAR(engine.renderScale(), 2.0f, 1e-6f);
  CHECK_EQ(engine.renderResolution().y, pixels.y * 2);
  engine.setRenderScale(1.0f);
  CHECK_EQ(engine.renderResolution().x, pixels.x);
}

TEST_CASE(window_placement_fallback) {
  WindowSystem &window = engine.getSystemManager().getSystem<WindowSystem>();
  // A place far from any display: not used, the size still is, and the window is centered
  WindowSystem::Placement lost;
  lost.x = 900000;
  lost.y = 900000;
  lost.width = 400;
  lost.height = 300;
  CHECK(!window.restorePlacement(lost));
  CHECK_EQ(window.getWindowSize().x, 400);
  CHECK_EQ(window.getWindowSize().y, 300);

  // One that is on the display is taken as it is
  SDL_Rect usable;
  CHECK(SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window.getWindow()), &usable));
  WindowSystem::Placement found;
  found.x = usable.x + 20;
  found.y = usable.y + 40;
  found.width = 320;
  found.height = 200;
  CHECK(window.restorePlacement(found));
  CHECK_EQ(window.getWindowSize().x, 320);
  const auto placement = window.getPlacement();
  CHECK(placement.has_value());
  CHECK_EQ(placement->width, 320);

  // A size that is not one is refused
  WindowSystem::Placement empty;
  CHECK(!window.restorePlacement(empty));
}
