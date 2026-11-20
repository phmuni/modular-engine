// Pause menu implementation: opening and closing, and a generic RmlUi data model over PlayerSettings. Nothing here
// names an option: what the menu shows and offers is in the document.

#include "systems/pauseMenu.h"

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/playerSettings.h"
#include "systems/saveData.h"
#include "systems/scriptSystem.h"
#include <RmlUi/Core.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr const char *MODEL = "pause_menu";
constexpr const char *PROJECT_DOCUMENT = "ui/pauseMenu.rml";
constexpr const char *ENGINE_DOCUMENT = "../assets/ui/engine/pauseMenu.rml";
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

struct PauseMenu::Impl {
  explicit Impl(Engine &e) : engine(e) { options.enabled = false; }

  // What a button of the document asked for: handled in update(), outside RmlUi's event dispatch
  struct Request {
    enum class Kind { Set, Step, Toggle, Show, Back, Resume, Quit, Action };
    Kind kind;
    std::string name;
    Value value = false;
    int direction = 1;
  };

  Engine &engine;
  PauseMenuOptions options;
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

  // --- The document ------------------------------------------------------------------------------------------------

  static bool exists(const std::string &assetPath) {
    std::error_code error;
    return std::filesystem::exists(EngineConfig::resolveAsset(assetPath), error);
  }

  // The option's file, else the project's, else the engine's
  std::string chooseDocument() const {
    if (!options.document.empty()) {
      if (exists(options.document))
        return options.document;
      LOG_W("[PauseMenu] The document %s does not exist; using the default one", options.document.c_str());
    }
    if (exists(PROJECT_DOCUMENT))
      return PROJECT_DOCUMENT;
    return ENGINE_DOCUMENT;
  }

  void bindEvents(Rml::DataModelConstructor &constructor) {
    using Kind = Request::Kind;
    const auto text = [](const Rml::VariantList &arguments, size_t index) {
      return index < arguments.size() ? arguments[index].Get<Rml::String>() : Rml::String();
    };
    const auto plain = [this, &constructor](const char *name, Kind kind) {
      constructor.BindEventCallback(name, [this, kind](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        requests.push_back({kind, {}});
      });
    };
    plain("resume", Kind::Resume);
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
  }

  bool load() {
    if (document)
      return true;
    Rml::Context *context = engine.getUIContext();
    if (!context)
      return false;
    Rml::DataModelConstructor constructor = context->CreateDataModel(MODEL);
    if (!constructor) {
      LOG_E("[PauseMenu] Could not create the data model");
      return false;
    }
    Rml::DataTypeRegister *types = constructor.GetDataTypeRegister();
    auto variantItem = std::make_unique<VariantDefinition>();
    Rml::VariableDefinition *variantPointer = variantItem.get();
    constructor.RegisterCustomDataVariableDefinition<NamedValues<Rml::Variant>>(
        std::make_unique<NamedValuesDefinition<Rml::Variant>>(variantPointer, std::move(variantItem)));
    constructor.RegisterCustomDataVariableDefinition<NamedValues<Rml::String>>(
        std::make_unique<NamedValuesDefinition<Rml::String>>(types->GetDefinition<Rml::String>()));
    constructor.RegisterCustomDataVariableDefinition<NamedValues<bool>>(
        std::make_unique<NamedValuesDefinition<bool>>(types->GetDefinition<bool>()));
    constructor.Bind("settings", &settingValues);
    constructor.Bind("labels", &labels);
    constructor.Bind("enabled", &enabledValues);
    constructor.Bind("screen", &screen);
    bindEvents(constructor);
    model = constructor.GetModelHandle();
    shown.clear();
    sync(); // the maps are filled before the document is read: it binds to what is in them

    const std::string path = chooseDocument();
    document = engine.loadUIDocument(path, false);
    if (!document) {
      context->RemoveDataModel(MODEL);
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
      context->RemoveDataModel(MODEL);
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
    for (const std::string &name : player.names()) {
      const auto seen = shown.find(name);
      auto entry = settingValues.items.find(name);
      if (entry != settingValues.items.end() && seen != shown.end() && !sameValue(toValue(entry->second), seen->second))
        player.set(name, toValue(entry->second)); // moved in the document
      const Value now = player.get(name);
      if (entry == settingValues.items.end() || !sameValue(toValue(entry->second), now)) {
        settingValues.items[name] = toVariant(now);
        valuesChanged = true;
      }
      if (seen == shown.end() || !sameValue(seen->second, now)) {
        labels.items[name] = player.label(name);
        shown[name] = now;
        labelsChanged = true;
      }
      const bool active = player.enabled(name);
      const auto flag = enabledValues.items.find(name);
      if (flag == enabledValues.items.end() || flag->second != active) {
        enabledValues.items[name] = active;
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

  void setOpen(const std::string &startScreen = {}) {
    if (open || !load())
      return;
    open = true;
    // The cursor and the pause are the menu's now; the game's come back when it closes
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
    shown.clear();
    sync();
    document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
    document->PullToFront(); // over the game's own documents, whenever they were loaded
    engine.scripts().pauseMenuOpened();
    if (openedCallback)
      openedCallback();
  }

  void setClosed() {
    if (!open)
      return;
    open = false;
    document->Hide();
    if (options.freeCursor)
      engine.setState(Toggle::CursorLock, wasCursorLocked);
    if (options.pause)
      engine.setState(Toggle::Paused, wasPaused);
    save().writeIfChanged();
    engine.scripts().pauseMenuClosed();
    if (closedCallback)
      closedCallback();
  }

  void showScreen(const std::string &name) {
    if (name.empty() || name == screen)
      return;
    screenStack.push_back(screen);
    screen = name;
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
    case Kind::Resume:
      setClosed();
      break;
    case Kind::Quit:
      engine.quit();
      break;
    case Kind::Action:
      engine.scripts().pauseMenuAction(request.name);
      if (actionCallback)
        actionCallback(request.name);
      break;
    }
  }

  void update(bool escapePressed) {
    if (escapePressed && options.escape) {
      if (open) // Escape steps back: from a screen to the one before, from the first one out of the menu
        requests.push_back({Request::Kind::Back, {}});
      else if (options.enabled && engine.getPlayMode() != Engine::PlayMode::Editing)
        setOpen();
    }
    if (!open)
      return;

    // A copy: a request may open or close things that add more
    std::vector<Request> pending;
    pending.swap(requests);
    for (const Request &request : pending) {
      if (!open)
        break;
      handle(request);
    }
    if (!open)
      return;
    sync();
    // Stays paused whatever else happened this frame
    if (options.pause)
      engine.setState(Toggle::Paused, true);
  }
};

PauseMenu::PauseMenu(Engine &engine) : m_impl(std::make_unique<Impl>(engine)) {}
PauseMenu::~PauseMenu() = default;

void PauseMenu::setEnabled(bool enabled) {
  PauseMenuOptions options = m_impl->options;
  options.enabled = enabled;
  configure(options);
}

bool PauseMenu::isEnabled() const { return m_impl->options.enabled; }

void PauseMenu::configure(const PauseMenuOptions &options) {
  Impl &impl = *m_impl;
  const bool documentChanged = options.document != impl.options.document;
  impl.options = options;
  if (documentChanged)
    impl.unload(); // loaded again below, or at the next open
  if (options.enabled)
    impl.load(); // so the first Escape does not wait for the document
}

const PauseMenuOptions &PauseMenu::options() const { return m_impl->options; }
void PauseMenu::open(const std::string &screen) { m_impl->setOpen(screen); }
void PauseMenu::close() { m_impl->setClosed(); }
bool PauseMenu::isOpen() const { return m_impl->open; }
bool PauseMenu::blocksInput() const { return m_impl->open && m_impl->options.blockInput; }
bool PauseMenu::showsCursor() const { return m_impl->open && m_impl->options.freeCursor; }
void PauseMenu::setCursorLockOnClose(bool locked) { m_impl->wasCursorLocked = locked; }
const std::string &PauseMenu::documentPath() const { return m_impl->loadedPath; }
void PauseMenu::onOpened(std::function<void()> callback) { m_impl->openedCallback = std::move(callback); }
void PauseMenu::onClosed(std::function<void()> callback) { m_impl->closedCallback = std::move(callback); }
void PauseMenu::onAction(std::function<void(const std::string &)> callback) {
  m_impl->actionCallback = std::move(callback);
}
void PauseMenu::update(bool escapePressed) { m_impl->update(escapePressed); }
