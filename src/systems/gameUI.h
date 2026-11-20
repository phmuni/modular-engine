#pragma once
// The game API for UI: changing the elements of the documents a game loaded, by id. Lua's `ui` table is this class
// (see systems/scriptSystem.cpp), reached from C++ as engine.ui(). Anything not covered here is RmlUi itself:
// engine.getUIContext() and engine.loadUIDocument() give the raw objects, and data models bind to those.
//
// Every call that names an element does nothing when there is none (a document not loaded yet, a typo).

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

  // The ready-made pause menu: Escape opens it once enabled (see Engine::enablePauseMenu)
  void pauseMenu(bool enabled = true);
  void openPauseMenu();
  void closePauseMenu();
  // Whether the pause menu is open
  bool isPaused() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
