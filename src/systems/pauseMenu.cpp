// The pause menu: a menu named "pause" (menu.cpp) and the options that were always its own.

#include "systems/pauseMenu.h"

#include "systems/menu.h"

PauseMenu::PauseMenu(MenuSystem &system, Menu &menu) : m_system(system), m_menu(menu) { m_options.enabled = false; }

void PauseMenu::setEnabled(bool enabled) {
  PauseMenuOptions options = m_options;
  options.enabled = enabled;
  configure(options);
}

bool PauseMenu::isEnabled() const { return m_options.enabled; }

void PauseMenu::configure(const PauseMenuOptions &options) {
  m_options = options;
  MenuOptions menu = m_menu.options();
  menu.document = options.document;
  menu.pause = options.pause;
  menu.freeCursor = options.freeCursor;
  menu.blockInput = options.blockInput;
  menu.escape = options.escape ? MenuEscape::Close : MenuEscape::None;
  m_menu.configure(menu); // a document that changed is loaded again
  if (options.enabled)
    m_menu.load(); // so the first Escape does not wait for the document
}

const PauseMenuOptions &PauseMenu::options() const { return m_options; }
void PauseMenu::open(const std::string &screen) { m_menu.open(screen); }
void PauseMenu::close() { m_menu.close(); }
bool PauseMenu::isOpen() const { return m_menu.isOpen(); }
bool PauseMenu::blocksInput() const { return m_menu.isOpen() && m_options.blockInput; }
bool PauseMenu::showsCursor() const { return m_menu.isOpen() && m_options.freeCursor; }
void PauseMenu::setCursorLockOnClose(bool locked) { m_system.setCursorLockOnClose(locked); }
const std::string &PauseMenu::documentPath() const { return m_menu.documentPath(); }
void PauseMenu::onOpened(std::function<void()> callback) { m_menu.onOpened(std::move(callback)); }
void PauseMenu::onClosed(std::function<void()> callback) { m_menu.onClosed(std::move(callback)); }
void PauseMenu::onAction(std::function<void(const std::string &)> callback) { m_menu.onAction(std::move(callback)); }
