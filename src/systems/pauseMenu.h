#pragma once
// The engine's ready-made pause menu: Resume, Options (volumes, window size, render scale, fullscreen, vsync) and Quit, an RmlUi document
// shipped with the engine (assets/ui/engine/pauseMenu.rml).
//
// Once enabled, Escape opens it (unless the editor's panels or the game's own UI want the key). Opening pauses the
// game like F7 does, frees the cursor and keeps keys and clicks from reaching the game; closing undoes all of that.
// The options are kept in the save data ("engine.*" keys) and applied the first time the menu is enabled.

#include "foundation/ecs/systemManager.h"
#include <memory>

class Engine;

class PauseMenu : public BaseSystem {
public:
  explicit PauseMenu(Engine &engine);
  ~PauseMenu() override;

  // Whether Escape opens the menu. Off by default; turning it on loads the menu and applies the saved options.
  void setEnabled(bool enabled);
  bool isEnabled() const;

  // From code, with or without Escape being enabled
  void open();
  void close();
  bool isOpen() const;
  // Whether the mouse is captured again when the menu closes (the game asked for a change while it was open)
  void setCursorLockOnClose(bool locked);

  // Once per frame, right after the input was read
  void update(bool escapePressed);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
