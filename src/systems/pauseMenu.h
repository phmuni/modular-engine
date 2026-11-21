#pragma once
// The pause menu: a menu (systems/menu.h, named "pause") with Escape to open and close it. The engine knows nothing
// about what is in it: the document is a file of the project (ui/pauseMenu.rml), written against the generic data
// model every menu has, which is described there.
//
// Behavior, each part configurable (PauseMenuOptions): Escape opens and closes it, opening pauses the game like F7
// does, frees the cursor and keeps keys and clicks from the game; closing undoes all of that.
//
// The data model is named "pause_menu". A document can use (see systems/menu.h for the rest, open('path') among it):
//   settings.<name>      the value of each PlayerSettings entry (a number, a boolean, or a choice's value);
//                        a numeric one also works as data-value="settings.masterVolume" on a range input
//   labels.<name>        its display text ("MSAA 4x", "75%", "On")
//   enabled.<name>       whether it matters now (FSR's sharpness while FSR is off is false)
//   screen               the screen shown ("main" when the menu opens); data-if="screen == 'options'"
//   events               set('name', value)  step('name', 1)  toggle('name')  show('screen')  back()
//                        resume()  quit()  action('name')   (back() on the first screen closes the menu)
// action('name') calls the game's callback: Lua's onMenuAction("pause", name), or the one given to onAction in C++
// (and onMenuAction in C++, like any menu's actions).
//
// The document is, in this order: PauseMenuOptions::document, the project's ui/pauseMenu.rml, or the engine's own
// copy (assets/ui/engine/pauseMenu.rml, what projects made before the file was part of the template use).

#include <functional>
#include <string>

class Menu;
class MenuSystem;

struct PauseMenuOptions {
  // Whether Escape opens it. Off by default in the engine; ui.pauseMenu{...} and ui.pauseMenu(true) turn it on.
  bool enabled = true;
  // The document, relative to assets/. Empty: the project's ui/pauseMenu.rml, else the engine's. One that does not
  // exist is a warning, and the same lookup follows.
  std::string document;
  // Opening pauses the game like F7 (physics, animation, scripts and time stand still). Off: the game keeps running
  // underneath the menu.
  bool pause = true;
  // Opening frees the mouse (and shows the cursor), and closing gives it back to the game as it was
  bool freeCursor = true;
  // Keys and clicks do not reach the game while it is open
  bool blockInput = true;
  // Escape opens it, steps back from a screen and closes it. Off: only code opens and closes it.
  bool escape = true;
};

// A view of the pause menu: what Engine::pauseMenu() gives
class PauseMenu {
public:
  PauseMenu(MenuSystem &system, Menu &menu);

  // Whether Escape opens the menu. Off by default; turning it on loads the menu's document.
  void setEnabled(bool enabled);
  bool isEnabled() const;
  // All of the options at once (a document that changed is loaded again)
  void configure(const PauseMenuOptions &options);
  const PauseMenuOptions &options() const;

  // From code, with or without Escape being enabled. The screen it starts on ("main" by default): a game's own
  // Options button can open the menu on "options"; back() and Escape then close it.
  void open(const std::string &screen = {});
  void close();
  bool isOpen() const;
  // Whether keys and clicks are kept from the game right now (open, and the blockInput option)
  bool blocksInput() const;
  // Whether the menu has the cursor right now (open, and the freeCursor option)
  bool showsCursor() const;
  // Whether the mouse is captured again when the menu closes (the game asked for a change while it was open)
  void setCursorLockOnClose(bool locked);
  // The document in use, as it was given to the UI (empty until it is loaded)
  const std::string &documentPath() const;

  // The game's callbacks (Lua's onMenuOpened, onMenuClosed and onMenuAction are called as well, with "pause")
  void onOpened(std::function<void()> callback);
  void onClosed(std::function<void()> callback);
  void onAction(std::function<void(const std::string &)> callback);

private:
  MenuSystem &m_system;
  Menu &m_menu;
  PauseMenuOptions m_options;
};
