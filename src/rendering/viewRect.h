#pragma once
// Where the game is drawn inside the window: the whole of it, or (with the editor docked) the Game panel's rectangle.

// In framebuffer pixels, measured from the window's top-left corner. Not `active`: the whole window.
struct ViewRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool active = false;

  bool operator==(const ViewRect &other) const {
    return active == other.active && (!active || (x == other.x && y == other.y && width == other.width && height == other.height));
  }
  bool operator!=(const ViewRect &other) const { return !(*this == other); }
};
