#pragma once
// Menus: RmlUi documents shown over the game that the engine knows nothing about. A menu is a file of the project (a
// main menu, a game over screen, an options screen, the pause menu), written against a generic data model, and the
// options here say how it behaves: whether it pauses the game, frees the cursor, keeps keys and clicks from the game,
// and what Escape does. Game code reaches them as ui.menu(path, options) in Lua and engine.ui().menu(path, options) in
// C++; the pause menu (systems/pauseMenu.h) is one of them, with Escape to open it.
//
// The data model. It is named like the document's file without its extension ("ui/mainMenu.rml" is "mainMenu"; the
// pause menu's is "pause_menu"), so a document starts with <body data-model="mainMenu">. Two menus cannot share a
// document at the same time. It has:
//   settings.<name>      the value of each PlayerSettings entry (a number, a boolean, or a choice's value);
//                        a numeric one also works as data-value="settings.masterVolume" on a range input
//   labels.<name>        its display text ("MSAA 4x", "75%", "On")
//   enabled.<name>       whether it matters now (FSR's sharpness while FSR is off is false)
//   screen               the screen shown ("main" when the menu opens); data-if="screen == 'options'"
//   events               set('name', value)  step('name', 1)  toggle('name')  show('screen')  back()  close()
//                        open('ui/options.rml')  action('name')  quit()      (resume() is close(), for the pause menu)
// show('screen') changes the screen inside the document; back() goes to the screen before, and from the first one
// closes the menu. open('path') opens another menu document on top: the new one takes the input, and closing it
// (back() or Escape) shows this one again.
// action('name') is how a button reaches the game: Lua's onMenuAction(menu, name), or the callback given to
// onMenuAction in C++ (menu is the menu's name; the pause menu also calls onPauseMenuAction(name)).
//
// A stack: only the menu opened last takes input. Opening one over another hides the one below; closing the top one
// shows it again, and closing one below the top closes everything above it.
//
// The menu's document may be shown at any time (ui.text("final-score", 12) finds its elements by id once the menu was
// opened the first time). Requests from buttons are queued and handled in update(), outside RmlUi's event dispatch.

#include "foundation/ecs/systemManager.h"
#include <functional>
#include <memory>
#include <string>

class Engine;
class MenuSystem;
class PauseMenu;

// What Escape does while the menu is the one on top
enum class MenuEscape {
  None,  // nothing: only buttons and code close it (a main menu)
  Back,  // steps back from a screen to the one before; on the first screen nothing
  Close, // steps back, and on the first screen closes the menu
  Quit   // steps back, and on the first screen quits the game
};

struct MenuOptions {
  // How callbacks call it (onMenuAction(menu, name)). Empty: the document's file name without its extension.
  std::string name;
  // The document, relative to assets/
  std::string document;
  // Opening pauses the game like F7 (physics, animation, scripts and time stand still); closing gives the pause back
  // as it was. Off: the game keeps running underneath the menu.
  bool pause = true;
  // Opening frees the mouse (and shows the cursor), and closing gives it back to the game as it was
  bool freeCursor = true;
  // Keys and clicks do not reach the game while it is open
  bool blockInput = true;
  MenuEscape escape = MenuEscape::Close;
};

class Menu {
public:
  Menu(MenuSystem &system, std::string name, std::string modelName);
  ~Menu();
  Menu(const Menu &) = delete;
  Menu &operator=(const Menu &) = delete;

  const std::string &name() const;
  const MenuOptions &options() const;
  // The options at once (a document that changed is loaded again)
  void configure(const MenuOptions &options);

  // Loads the document now, hidden (open() does it as well); false if it could not be loaded
  bool load();
  // Opens it (on top of the menus open) on its first screen ("main" by default), or on one of the document's screens.
  // Does nothing if it is open or its document could not be loaded.
  void open(const std::string &screen = {});
  // Closes it, with everything opened above it; the menu below it shows again
  void close();
  bool isOpen() const;
  // While open: goes to a screen of the document, as the document's own show('screen') does
  void show(const std::string &screen);
  // The screen shown now
  const std::string &screen() const;
  // The document in use, as it was given to the UI (empty until it is loaded)
  const std::string &documentPath() const;

  // This menu's own callbacks, besides the game's (see MenuSystem)
  void onOpened(std::function<void()> callback);
  void onClosed(std::function<void()> callback);
  void onAction(std::function<void(const std::string &)> callback);

private:
  friend class MenuSystem;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

class MenuSystem : public BaseSystem {
public:
  explicit MenuSystem(Engine &engine);
  ~MenuSystem() override;

  // The menu for a document: made the first time, configured with the options (a menu already there for that name
  // is the same one). The document loads now, hidden; open() shows it.
  Menu &menu(const std::string &path, MenuOptions options = {});
  // By name; null if there is none
  Menu *find(const std::string &name);
  // The pause menu, a menu named "pause" with Escape to open it
  PauseMenu &pauseMenu();
  Menu &pause();

  // Whether a menu is open, and the name of the one on top (empty if none)
  bool anyOpen() const;
  const std::string &topName() const;
  // Whether keys and clicks are kept from the game right now (a menu is open with blockInput)
  bool blocksInput() const;
  // Whether a menu has the cursor right now (a menu is open with freeCursor)
  bool showsCursor() const;
  // Whether the mouse is captured again when the menus close (the game asked for a change while one was open)
  void setCursorLockOnClose(bool locked);
  // Closes every open menu, top first (a play session ending)
  void closeAll();

  // The game's callbacks for every menu, with the menu's name (Lua's onMenuOpened(menu), onMenuClosed(menu) and
  // onMenuAction(menu, name) are called as well)
  void onOpened(std::function<void(const std::string &)> callback);
  void onClosed(std::function<void(const std::string &)> callback);
  void onAction(std::function<void(const std::string &, const std::string &)> callback);

  // Once per frame, right after the input was read
  void update(bool escapePressed);

private:
  friend class Menu;
  // A document's open('path'): the menu made for that file, or a new one that leaves pause and cursor to the menu below
  void openDocument(const std::string &path);
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
