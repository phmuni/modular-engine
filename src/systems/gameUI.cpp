// The game API for UI over RmlUi: see gameUI.h.

#include "systems/gameUI.h"

#include "foundation/core/engine.h"
#include <RmlUi/Core.h>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace {
class ClickListener final : public Rml::EventListener {
public:
  explicit ClickListener(std::function<void()> handler) : m_handler(std::move(handler)) {}
  void ProcessEvent(Rml::Event &) override { m_handler(); }

private:
  std::function<void()> m_handler;
};
} // namespace

struct GameUI::Impl {
  Engine &engine;
  std::unordered_map<std::string, Rml::ElementDocument *> documents; // by path as written
  std::vector<std::unique_ptr<ClickListener>> listeners;

  explicit Impl(Engine &e) : engine(e) {}

  Rml::Element *find(const std::string &id) {
    Rml::Context *context = engine.getUIContext();
    if (!context)
      return nullptr;
    for (int i = 0; i < context->GetNumDocuments(); ++i)
      if (Rml::Element *element = context->GetDocument(i)->GetElementById(id))
        return element;
    return nullptr;
  }
};

GameUI::GameUI(Engine &engine) : m_impl(std::make_unique<Impl>(engine)) {}
GameUI::~GameUI() = default;

bool GameUI::load(const std::string &path) {
  auto it = m_impl->documents.find(path);
  if (it != m_impl->documents.end()) {
    it->second->Show();
    return true;
  }
  Rml::ElementDocument *document = m_impl->engine.loadUIDocument(path);
  if (document)
    m_impl->documents[path] = document;
  return document != nullptr;
}

void GameUI::text(const std::string &id, const std::string &value) {
  if (Rml::Element *element = m_impl->find(id))
    element->SetInnerRML(value);
}

void GameUI::text(const std::string &id, int value) { text(id, std::to_string(value)); }

void GameUI::text(const std::string &id, double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.14g", value);
  text(id, std::string(buffer));
}

void GameUI::show(const std::string &id, bool visible) {
  if (Rml::Element *element = m_impl->find(id)) {
    if (visible)
      element->RemoveProperty("display");
    else
      element->SetProperty("display", "none");
  }
}

void GameUI::setClass(const std::string &id, const std::string &name, bool on) {
  if (Rml::Element *element = m_impl->find(id))
    element->SetClass(name, on);
}

void GameUI::style(const std::string &id, const std::string &property, const std::string &value) {
  if (Rml::Element *element = m_impl->find(id)) {
    if (value.empty())
      element->RemoveProperty(property);
    else
      element->SetProperty(property, value);
  }
}

void GameUI::attribute(const std::string &id, const std::string &name, const std::string &value) {
  if (Rml::Element *element = m_impl->find(id))
    element->SetAttribute(name, value);
}

bool GameUI::onClick(const std::string &id, std::function<void()> handler) {
  Rml::Element *element = m_impl->find(id);
  if (!element)
    return false;
  m_impl->listeners.push_back(std::make_unique<ClickListener>(std::move(handler)));
  element->AddEventListener(Rml::EventId::Click, m_impl->listeners.back().get());
  return true;
}

bool GameUI::loadFont(const std::string &path, bool fallback) { return m_impl->engine.loadUIFont(path, fallback); }

void GameUI::pauseMenu(bool enabled) { m_impl->engine.enablePauseMenu(enabled); }
void GameUI::openPauseMenu(const std::string &screen) { m_impl->engine.pauseMenu().open(screen); }
void GameUI::closePauseMenu() { m_impl->engine.closePauseMenu(); }
bool GameUI::isPaused() const { return m_impl->engine.isPauseMenuOpen(); }
bool GameUI::isPauseMenuOpen() const { return m_impl->engine.isPauseMenuOpen(); }
void GameUI::pauseMenu(const PauseMenuOptions &options) { m_impl->engine.pauseMenu().configure(options); }
void GameUI::onPauseMenuOpened(std::function<void()> callback) {
  m_impl->engine.pauseMenu().onOpened(std::move(callback));
}
void GameUI::onPauseMenuClosed(std::function<void()> callback) {
  m_impl->engine.pauseMenu().onClosed(std::move(callback));
}
void GameUI::onPauseMenuAction(std::function<void(const std::string &)> callback) {
  m_impl->engine.pauseMenu().onAction(std::move(callback));
}

Menu &GameUI::menu(const std::string &path, const MenuOptions &options) {
  Menu &menu = m_impl->engine.menus().menu(path, options);
  menu.open();
  return menu;
}
void GameUI::onMenuOpened(std::function<void(const std::string &)> callback) {
  m_impl->engine.menus().onOpened(std::move(callback));
}
void GameUI::onMenuClosed(std::function<void(const std::string &)> callback) {
  m_impl->engine.menus().onClosed(std::move(callback));
}
void GameUI::onMenuAction(std::function<void(const std::string &, const std::string &)> callback) {
  m_impl->engine.menus().onAction(std::move(callback));
}

void GameUI::loadingScreen(const LoadingScreenOptions &options) { m_impl->engine.loadingScreen().configure(options); }
void GameUI::loadingScreen(bool enabled) {
  if (enabled)
    m_impl->engine.loadingScreen().configure({});
  else
    m_impl->engine.loadingScreen().disable();
}
