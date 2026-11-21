#pragma once
// The game API for UI: changing the elements of the documents a game loaded, by id. Lua's `ui` table is this class
// (see systems/lua), reached from C++ as engine.ui(). Anything not covered here is RmlUi itself:
// engine.getUIContext() and engine.loadUIDocument() give the raw objects, and data models bind to those.
//
// Every call that names an element does nothing when there is none (a document not loaded yet, a typo).

#include "systems/loadingScreen.h"
#include "systems/menu.h"
#include "systems/pauseMenu.h"
#include <functional>
#include <memory>
#include <string>

class Engine;

class GameUI {
public:
  explicit GameUI(Engine &engine);
  ~GameUI();
  GameUI(const GameUI &) = delete;
  GameUI &operator=(const GameUI &) = delete;

  // Loads an RML document (path relative to assets/) and shows it. A document is loaded once: loading it again just
  // shows the same one, so a game script that runs again (after Stop) does not stack copies. False if it failed.
  bool load(const std::string &path);

  // Loads a font file (path relative to assets/); documents use it by the family name inside the file. With `fallback`
  // it also serves glyphs the other fonts lack (symbols, other scripts). False if the file could not be read.
  bool loadFont(const std::string &path, bool fallback = false);

  // Sets an element's content: a string, or a number written plainly (12, 1.5)
  void text(const std::string &id, const std::string &value);
  void text(const std::string &id, int value);
  void text(const std::string &id, double value);
  // Shows or hides it (display: none), keeping its place in the document
  void show(const std::string &id, bool visible);
  // Adds or removes a CSS class, to restyle it from the document's RCSS
  void setClass(const std::string &id, const std::string &name, bool on);
  // One RCSS property ("color", "#ff0000"; "width", "50%"); an empty value goes back to the stylesheet's
  void style(const std::string &id, const std::string &property, const std::string &value);
  // An attribute ("src" of an image, "value" of an input)
  void attribute(const std::string &id, const std::string &name, const std::string &value);
  // Calls the handler whenever the element is clicked; false if there is no such element
  bool onClick(const std::string &id, std::function<void()> handler);
  // Clicks the first element that is shown and matches an RCSS selector ("button.quit", "#play",
  // "[data-event-click*=options]"), as if the player had: its data-event-click runs. For driving a game's menus from a
  // script (smoke tests); false if nothing shown matches.
  bool click(const std::string &selector);
  // Whether an element that is shown (not hidden, nor in a hidden document) matches the selector
  bool isVisible(const std::string &selector);

  // The pause menu (see systems/pauseMenu.h): a document of the project's, which Escape opens once enabled. It is a
  // Menu (named "pause"): pauseMenu().open("options"), pauseMenu().isOpen(), close(); that works without Escape being
  // enabled. With no argument nothing about it changes. pauseMenu(true) takes the defaults; the options choose the
  // document, whether it pauses the game, frees the cursor, blocks the game's input, and whether Escape opens it.
  // Each also returns the Menu, whose onOpened, onClosed and onAction take the game's callbacks for this menu
  // (pauseMenu().onAction(...)); onMenuAction below gets it too, with "pause".
  Menu &pauseMenu();
  Menu &pauseMenu(bool enabled);
  Menu &pauseMenu(const PauseMenuOptions &options);

  // Menus (see systems/menu.h): any document of the project shown over the game, with options for pausing, the cursor,
  // the game's input and Escape. Makes the menu for that document (calling it again with other options changes them),
  // opens it, and returns it: close(), isOpen(), show("screen"), open() again later. A button of the document with
  // action('name') reaches the game's callback below, or onMenuAction(menu, name) in a Lua game script. The menu's
  // name is the file's without its extension unless options.name says otherwise.
  Menu &menu(const std::string &path, const MenuOptions &options = {});
  // The game's callbacks for every menu, with the menu's name (Lua scripts define onMenuOpened(menu),
  // onMenuClosed(menu) and onMenuAction(menu, name) instead)
  void onMenuOpened(std::function<void(const std::string &)> callback);
  void onMenuClosed(std::function<void(const std::string &)> callback);
  void onMenuAction(std::function<void(const std::string &, const std::string &)> callback);

  // The loading screen (see systems/loadingScreen.h): a document shown when loadScene takes more than a moment, with
  // the load's progress in its data model. loadingScreen(false) turns it off again.
  void loadingScreen(const LoadingScreenOptions &options);
  void loadingScreen(bool enabled = true);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
