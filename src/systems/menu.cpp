// Menus: opening and closing, the stack, and a generic RmlUi data model over PlayerSettings. Nothing here names an
// option or a screen: what a menu shows and offers is in its document.

#include "systems/menu.h"

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/pauseMenu.h"
#include "systems/playerSettings.h"
#include "systems/saveData.h"
#include "systems/scriptSystem.h"
#include <RmlUi/Core.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr const char *MAIN_SCREEN = "main";

using Value = PlayerSettings::Value;

// A set of values by name, for a data model to reach as `name.key` (RmlUi has no map of its own in this version)
template <typename T> struct NamedValues {
  std::map<Rml::String, T> items;
};

template <typename T> class NamedValuesDefinition final : public Rml::VariableDefinition {
public:
  NamedValuesDefinition(Rml::VariableDefinition *item, Rml::UniquePtr<Rml::VariableDefinition> owned = nullptr)
      : Rml::VariableDefinition(Rml::DataVariableType::Struct), m_item(item), m_owned(std::move(owned)) {}

  Rml::DataVariable Child(void *ptr, const Rml::DataAddressEntry &address) override {
    auto &items = static_cast<NamedValues<T> *>(ptr)->items;
    const auto found = items.find(address.name);
    return found == items.end() ? Rml::DataVariable() : Rml::DataVariable(m_item, &found->second);
  }

private:
  Rml::VariableDefinition *m_item;
  Rml::UniquePtr<Rml::VariableDefinition> m_owned;
};

// A value of whatever type the setting has. A write from the document (a slider gives its value as text) keeps the type
// the value had.
class VariantDefinition final : public Rml::VariableDefinition {
public:
  VariantDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Scalar) {}
  bool Get(void *ptr, Rml::Variant &variant) override {
    variant = *static_cast<Rml::Variant *>(ptr);
    return true;
  }
  bool Set(void *ptr, const Rml::Variant &variant) override {
    Rml::Variant &target = *static_cast<Rml::Variant *>(ptr);
    switch (target.GetType()) {
    case Rml::Variant::BOOL: {
      bool value = false;
      if (!variant.GetInto(value))
        return false;
      target = value;
      return true;
    }
    case Rml::Variant::STRING: {
      Rml::String value;
      if (!variant.GetInto(value))
        return false;
      target = value;
      return true;
    }
    default: {
      double value = 0.0;
      if (!variant.GetInto(value))
        return false;
      target = value;
      return true;
    }
    }
  }
};

Value toValue(const Rml::Variant &variant) {
  switch (variant.GetType()) {
  case Rml::Variant::BOOL:
    return Value(variant.Get<bool>());
  case Rml::Variant::STRING:
    return Value(variant.Get<Rml::String>());
  default:
    return Value(variant.Get<double>());
  }
}

Rml::Variant toVariant(const Value &value) {
  if (const bool *flag = std::get_if<bool>(&value))
    return Rml::Variant(*flag);
  if (const double *number = std::get_if<double>(&value))
    return Rml::Variant(*number);
  return Rml::Variant(std::get<std::string>(value));
}

bool sameValue(const Value &a, const Value &b) {
  if (a.index() != b.index())
    return false;
  if (const double *number = std::get_if<double>(&a))
    return std::abs(*number - std::get<double>(b)) < 1e-9;
  return a == b;
}
} // namespace

namespace {
std::string stemOf(const std::string &path) { return std::filesystem::path(path).stem().string(); }
} // namespace

// --- The system's state, before the menus' (they use it) ---------------------------------------------------------------

struct MenuSystem::Impl {
  explicit Impl(Engine &e) : engine(e) {}

  Engine &engine;
  std::vector<std::unique_ptr<Menu>> menus; // never removed: handles stay valid
  std::vector<Menu *> stack;                // the open ones, the one on top last
  std::unique_ptr<PauseMenu> pauseFacade;
  Menu *pauseMenu = nullptr;

  std::function<void(const std::string &)> openedCallback;
  std::function<void(const std::string &)> closedCallback;
  std::function<void(const std::string &, const std::string &)> actionCallback;
};

// --- One menu ----------------------------------------------------------------------------------------------------------

struct Menu::Impl {
  Impl(Menu &m, MenuSystem &s, std::string n, std::string model)
      : self(m), system(s), engine(s.m_impl->engine), name(std::move(n)), modelName(std::move(model)) {}

  // What a button of the document asked for: handled in update(), outside RmlUi's event dispatch
  struct Request {
    enum class Kind { Set, Step, Toggle, Show, Back, Close, Quit, Action, Open, Escape };
    Kind kind;
    std::string name;
    Value value = false;
    int direction = 1;
  };

  Menu &self;
  MenuSystem &system;
  Engine &engine;
  std::string name;
  std::string modelName;
  MenuOptions options;
  std::vector<std::string> fallbacks; // documents to use when the option's is missing (the pause menu's)
  bool isPause = false;               // the pause menu: its data model keeps its name when the document changes
  bool open = false;
  bool wasCursorLocked = false;
  bool wasPaused = false;
  Rml::ElementDocument *document = nullptr;
  Rml::DataModelHandle model;
  std::string loadedPath; // as it was given to the UI

  // The model's variables
  NamedValues<Rml::Variant> settingValues;
  NamedValues<Rml::String> labels;
  NamedValues<bool> enabledValues;
  Rml::String screen = MAIN_SCREEN;
  std::vector<std::string> screenStack; // the screens to go back to
  std::map<std::string, Value> shown;   // what the document was last given, to tell its own edits from the game's
  std::vector<Request> requests;

  std::function<void()> openedCallback;
  std::function<void()> closedCallback;
  std::function<void(const std::string &)> actionCallback;

  PlayerSettings &settings() { return engine.settings(); }
  SaveData &save() { return engine.saveData(); }
  std::vector<Menu *> &stack() { return system.m_impl->stack; }

  // --- The document ------------------------------------------------------------------------------------------------

  static bool exists(const std::string &assetPath) {
    std::error_code error;
    return std::filesystem::exists(EngineConfig::resolveAsset(assetPath), error);
  }

  // The option's file, else the fallbacks (the project's, then the engine's); empty if there is none
  std::string chooseDocument() const {
    if (!options.document.empty()) {
      if (exists(options.document))
        return options.document;
      LOG_W("[Menu] The document %s of the menu %s does not exist%s", options.document.c_str(), name.c_str(),
            fallbacks.empty() ? "" : "; using the default one");
    }
    for (const std::string &candidate : fallbacks)
      if (candidate == fallbacks.back() || exists(candidate))
        return candidate;
    return {};
  }

  void bindEvents(Rml::DataModelConstructor &constructor) {
    using Kind = Request::Kind;
    const auto text = [](const Rml::VariantList &arguments, size_t index) {
      return index < arguments.size() ? arguments[index].Get<Rml::String>() : Rml::String();
    };
    const auto plain = [this, &constructor](const char *event, Kind kind) {
      constructor.BindEventCallback(event, [this, kind](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        requests.push_back({kind, {}});
      });
    };
    plain("resume", Kind::Close);
    plain("close", Kind::Close);
    plain("quit", Kind::Quit);
    plain("back", Kind::Back);
    constructor.BindEventCallback("set", [this, text](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &arguments) {
      if (arguments.size() >= 2)
        requests.push_back({Kind::Set, text(arguments, 0), toValue(arguments[1])});
    });
    constructor.BindEventCallback("step", [this, text](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &arguments) {
      Request request{Kind::Step, text(arguments, 0)};
      request.direction = arguments.size() >= 2 && arguments[1].Get<double>() < 0.0 ? -1 : 1;
      requests.push_back(request);
    });
    const auto named = [this, text, &constructor](const char *event, Kind kind) {
      constructor.BindEventCallback(event, [this, text, kind](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &arguments) {
        requests.push_back({kind, text(arguments, 0)});
      });
    };
    named("toggle", Kind::Toggle);
    named("show", Kind::Show);
    named("action", Kind::Action);
    named("open", Kind::Open);
  }

  bool load() {
    if (document)
      return true;
    Rml::Context *context = engine.getUIContext();
    if (!context)
      return false;
    const std::string path = chooseDocument();
    if (path.empty())
      return false;
    Rml::DataModelConstructor constructor = context->CreateDataModel(modelName);
    if (!constructor) {
      LOG_E("[Menu] Could not create the data model %s", modelName.c_str());
      return false;
    }
    Rml::DataTypeRegister *types = constructor.GetDataTypeRegister();
    auto variantItem = std::make_unique<VariantDefinition>();
    Rml::VariableDefinition *variantPointer = variantItem.get();
    // The types belong to the context, shared by every menu's model: the first menu loaded registers them, and the
    // register keeps what it has (the constructor's own Register... would log an error for each later menu)
    types->RegisterDefinition(Rml::Family<NamedValues<Rml::Variant>>::Id(),
                              std::make_unique<NamedValuesDefinition<Rml::Variant>>(variantPointer, std::move(variantItem)));
    types->RegisterDefinition(Rml::Family<NamedValues<Rml::String>>::Id(),
                              std::make_unique<NamedValuesDefinition<Rml::String>>(types->GetDefinition<Rml::String>()));
    types->RegisterDefinition(Rml::Family<NamedValues<bool>>::Id(),
                              std::make_unique<NamedValuesDefinition<bool>>(types->GetDefinition<bool>()));
    constructor.Bind("settings", &settingValues);
    constructor.Bind("labels", &labels);
    constructor.Bind("enabled", &enabledValues);
    constructor.Bind("screen", &screen);
    bindEvents(constructor);
    model = constructor.GetModelHandle();
    shown.clear();
    sync(); // the maps are filled before the document is read: it binds to what is in them

    document = engine.loadUIDocument(path, false);
    if (!document) {
      context->RemoveDataModel(modelName);
      return false;
    }
    loadedPath = path;
    return true;
  }

  void unload() {
    if (open)
      setClosed();
    if (!document)
      return;
    if (Rml::Context *context = engine.getUIContext()) {
      context->UnloadDocument(document);
      context->RemoveDataModel(modelName);
    }
    document = nullptr;
    loadedPath.clear();
  }

  // --- The model follows PlayerSettings -----------------------------------------------------------------------------

  // The document's edits (a slider moved) go to the settings; what the settings hold now (the game may have changed
  // it, or the edit was rounded) goes to the document
  void sync() {
    bool valuesChanged = false, labelsChanged = false, enabledChanged = false;
    PlayerSettings &player = settings();
    for (const std::string &key : player.names()) {
      const auto seen = shown.find(key);
      auto entry = settingValues.items.find(key);
      if (entry != settingValues.items.end() && seen != shown.end() && !sameValue(toValue(entry->second), seen->second))
        player.set(key, toValue(entry->second)); // moved in the document
      const Value now = player.get(key);
      if (entry == settingValues.items.end() || !sameValue(toValue(entry->second), now)) {
        settingValues.items[key] = toVariant(now);
        valuesChanged = true;
      }
      if (seen == shown.end() || !sameValue(seen->second, now)) {
        labels.items[key] = player.label(key);
        shown[key] = now;
        labelsChanged = true;
      }
      const bool active = player.enabled(key);
      const auto flag = enabledValues.items.find(key);
      if (flag == enabledValues.items.end() || flag->second != active) {
        enabledValues.items[key] = active;
        enabledChanged = true;
      }
    }
    if (valuesChanged)
      model.DirtyVariable("settings");
    if (labelsChanged)
      model.DirtyVariable("labels");
    if (enabledChanged)
      model.DirtyVariable("enabled");
  }

  // --- Open and close ----------------------------------------------------------------------------------------------

  void display() {
    shown.clear();
    sync();
    document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
    document->PullToFront(); // over the game's own documents, whenever they were loaded
  }

  void setOpen(const std::string &startScreen = {}) {
    if (open || !load())
      return;
    std::vector<Menu *> &menus = stack();
    if (!menus.empty())
      menus.back()->m_impl->document->Hide(); // the one below waits, out of sight
    open = true;
    menus.push_back(&self);
    // The cursor and the pause are the menu's now; the game's (or the menu's below) come back when it closes
    wasCursorLocked = engine.getState(Toggle::CursorLock);
    wasPaused = engine.getState(Toggle::Paused);
    if (options.freeCursor)
      engine.setState(Toggle::CursorLock, false);
    if (options.pause)
      engine.setState(Toggle::Paused, true);
    requests.clear();
    screenStack.clear();
    screen = startScreen.empty() ? MAIN_SCREEN : startScreen;
    model.DirtyVariable("screen");
    display();
    engine.scripts().menuOpened(name);
    if (openedCallback)
      openedCallback();
    if (system.m_impl->openedCallback)
      system.m_impl->openedCallback(name);
  }

  void setClosed() {
    if (!open)
      return;
    std::vector<Menu *> &menus = stack();
    while (menus.back() != &self) // what was opened over this one goes first
      menus.back()->m_impl->setClosed();
    open = false;
    menus.pop_back();
    document->Hide();
    if (options.freeCursor)
      engine.setState(Toggle::CursorLock, wasCursorLocked);
    if (options.pause)
      engine.setState(Toggle::Paused, wasPaused);
    save().writeIfChanged();
    if (!menus.empty()) { // the one below is the one on top again
      menus.back()->m_impl->requests.clear();
      menus.back()->m_impl->display();
    }
    engine.scripts().menuClosed(name);
    if (closedCallback)
      closedCallback();
    if (system.m_impl->closedCallback)
      system.m_impl->closedCallback(name);
  }

  void showScreen(const std::string &target) {
    if (target.empty() || target == screen)
      return;
    screenStack.push_back(screen);
    screen = target;
    model.DirtyVariable("screen");
  }

  void goBack() {
    if (screenStack.empty()) { // the first screen: out of the menu
      setClosed();
      return;
    }
    screen = screenStack.back();
    screenStack.pop_back();
    model.DirtyVariable("screen");
    save().writeIfChanged();
  }

  void pressEscape() {
    switch (options.escape) {
    case MenuEscape::None:
      break;
    case MenuEscape::Back:
      if (!screenStack.empty())
        goBack();
      break;
    case MenuEscape::Close:
      goBack(); // the first screen closes it
      break;
    case MenuEscape::Quit:
      if (screenStack.empty())
        engine.quit();
      else
        goBack();
      break;
    }
  }

  void handle(const Request &request) {
    using Kind = Request::Kind;
    PlayerSettings &player = settings();
    switch (request.kind) {
    case Kind::Set:
      player.set(request.name, request.value);
      break;
    case Kind::Step:
      player.step(request.name, request.direction);
      break;
    case Kind::Toggle:
      player.step(request.name, 1); // a boolean flips; a list goes to the next
      break;
    case Kind::Show:
      showScreen(request.name);
      break;
    case Kind::Back:
      goBack();
      break;
    case Kind::Close:
      setClosed();
      break;
    case Kind::Quit:
      engine.quit();
      break;
    case Kind::Escape:
      pressEscape();
      break;
    case Kind::Open:
      system.openDocument(request.name);
      break;
    case Kind::Action:
      engine.scripts().menuAction(name, request.name);
      if (actionCallback)
        actionCallback(request.name);
      if (system.m_impl->actionCallback)
        system.m_impl->actionCallback(name, request.name);
      break;
    }
  }

  void processRequests() {
    // A copy: a request may open or close things that add more
    std::vector<Request> pending;
    pending.swap(requests);
    for (const Request &request : pending) {
      if (!open)
        break;
      handle(request);
    }
  }
};

Menu::Menu(MenuSystem &system, std::string name, std::string modelName)
    : m_impl(std::make_unique<Impl>(*this, system, std::move(name), std::move(modelName))) {}
Menu::~Menu() = default;

const std::string &Menu::name() const { return m_impl->name; }
const MenuOptions &Menu::options() const { return m_impl->options; }

void Menu::configure(const MenuOptions &options) {
  Impl &impl = *m_impl;
  const bool documentChanged = options.document != impl.options.document;
  impl.options = options;
  impl.options.name = impl.name;
  if (documentChanged) {
    impl.unload(); // loaded again at the next open
    if (!impl.isPause)
      impl.modelName = stemOf(options.document); // the document's file name: <body data-model="mainMenu">
  }
}

bool Menu::load() { return m_impl->load(); }
void Menu::open(const std::string &screen) { m_impl->setOpen(screen); }
void Menu::close() { m_impl->setClosed(); }
bool Menu::isOpen() const { return m_impl->open; }
void Menu::show(const std::string &screen) {
  if (m_impl->open)
    m_impl->showScreen(screen);
}
const std::string &Menu::screen() const { return m_impl->screen; }
const std::string &Menu::documentPath() const { return m_impl->loadedPath; }
void Menu::onOpened(std::function<void()> callback) { m_impl->openedCallback = std::move(callback); }
void Menu::onClosed(std::function<void()> callback) { m_impl->closedCallback = std::move(callback); }
void Menu::onAction(std::function<void(const std::string &)> callback) { m_impl->actionCallback = std::move(callback); }

// --- The system --------------------------------------------------------------------------------------------------------

MenuSystem::MenuSystem(Engine &engine) : m_impl(std::make_unique<Impl>(engine)) {
  Impl &impl = *m_impl;
  auto pause = std::make_unique<Menu>(*this, "pause", "pause_menu");
  impl.pauseMenu = pause.get();
  pause->m_impl->isPause = true;
  pause->m_impl->fallbacks = {"ui/pauseMenu.rml", "../assets/ui/engine/pauseMenu.rml"};
  impl.menus.push_back(std::move(pause));
  impl.pauseFacade = std::make_unique<PauseMenu>(*this, *impl.pauseMenu);
}

MenuSystem::~MenuSystem() = default;

Menu &MenuSystem::menu(const std::string &path, MenuOptions options) {
  Impl &impl = *m_impl;
  if (options.name.empty())
    options.name = stemOf(path);
  options.document = path;
  Menu *found = find(options.name);
  if (!found) {
    impl.menus.push_back(std::make_unique<Menu>(*this, options.name, stemOf(path)));
    found = impl.menus.back().get();
  }
  found->configure(options);
  found->load();
  return *found;
}

// A document asked for another one: the menu already made for it, else one that leaves the game's state to the menu it
// was opened from (no pause, no cursor change)
void MenuSystem::openDocument(const std::string &path) {
  for (const auto &menu : m_impl->menus)
    if (menu->m_impl->options.document == path) {
      menu->open();
      return;
    }
  MenuOptions options;
  options.pause = false;
  options.freeCursor = false;
  menu(path, options).open();
}

Menu *MenuSystem::find(const std::string &name) {
  for (const auto &menu : m_impl->menus)
    if (menu->name() == name)
      return menu.get();
  return nullptr;
}

PauseMenu &MenuSystem::pauseMenu() { return *m_impl->pauseFacade; }
Menu &MenuSystem::pause() { return *m_impl->pauseMenu; }
bool MenuSystem::anyOpen() const { return !m_impl->stack.empty(); }

const std::string &MenuSystem::topName() const {
  static const std::string none;
  return m_impl->stack.empty() ? none : m_impl->stack.back()->name();
}

bool MenuSystem::blocksInput() const {
  for (const Menu *menu : m_impl->stack)
    if (menu->options().blockInput)
      return true;
  return false;
}

bool MenuSystem::showsCursor() const {
  for (const Menu *menu : m_impl->stack)
    if (menu->options().freeCursor)
      return true;
  return false;
}

void MenuSystem::setCursorLockOnClose(bool locked) {
  for (Menu *menu : m_impl->stack) // the lowest one that took the cursor gives it back last
    if (menu->options().freeCursor) {
      menu->m_impl->wasCursorLocked = locked;
      return;
    }
}

void MenuSystem::closeAll() {
  while (!m_impl->stack.empty())
    m_impl->stack.back()->close();
}

void MenuSystem::onOpened(std::function<void(const std::string &)> callback) {
  m_impl->openedCallback = std::move(callback);
}
void MenuSystem::onClosed(std::function<void(const std::string &)> callback) {
  m_impl->closedCallback = std::move(callback);
}
void MenuSystem::onAction(std::function<void(const std::string &, const std::string &)> callback) {
  m_impl->actionCallback = std::move(callback);
}

void MenuSystem::update(bool escapePressed) {
  Impl &impl = *m_impl;
  if (escapePressed) {
    if (!impl.stack.empty()) // Escape is the top menu's alone
      impl.stack.back()->m_impl->requests.push_back({Menu::Impl::Request::Kind::Escape, {}});
    else if (impl.pauseFacade->isEnabled() && impl.pauseMenu->options().escape != MenuEscape::None &&
             !impl.engine.isFrozen())
      impl.pauseMenu->open();
  }
  if (impl.stack.empty())
    return;

  const std::vector<Menu *> open = impl.stack; // a request may open and close menus
  for (Menu *menu : open)
    if (menu->isOpen())
      menu->m_impl->processRequests();
  if (impl.stack.empty())
    return;
  impl.stack.back()->m_impl->sync();
  // Stays paused whatever else happened this frame
  for (const Menu *menu : impl.stack)
    if (menu->options().pause)
      impl.engine.setState(Toggle::Paused, true);
}
