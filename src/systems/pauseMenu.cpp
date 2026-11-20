// Pause menu implementation: an RmlUi document with a data model, and the engine state it pauses and changes.

#include "systems/pauseMenu.h"

#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/audioSystem.h"
#include "systems/saveData.h"
#include "systems/windowSystem.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr const char *DOCUMENT = "../assets/ui/engine/pauseMenu.rml";

// Where the options live in the save data
constexpr const char *KEY_MASTER = "engine.volume.master";
constexpr const char *KEY_MUSIC = "engine.volume.music";
constexpr const char *KEY_EFFECTS = "engine.volume.effects";
constexpr const char *KEY_FULLSCREEN = "engine.fullscreen";
constexpr const char *KEY_VSYNC = "engine.vsync";

int toPercent(float volume) { return std::clamp(static_cast<int>(std::lround(volume * 100.0f)), 0, 100); }
// 0.49 in the file, not the 0.49000000953674316 a float widens to
double percentToSaved(int percent) { return static_cast<double>(percent) / 100.0; }
} // namespace

struct PauseMenu::Impl {
  explicit Impl(Engine &e) : engine(e) {}

  Engine &engine;
  bool enabled = false;
  bool open = false;
  bool loaded = false; // the document and its model exist
  bool optionsApplied = false;
  Rml::ElementDocument *document = nullptr;
  Rml::DataModelHandle model;

  // What the document shows and edits; sliders are 0 to 100
  int master = 100;
  int music = 100;
  int effects = 100;
  bool fullscreen = false;
  bool vsync = true;
  bool options = false; // the Options screen instead of the main one

  // What buttons asked for: handled in update(), outside RmlUi's event dispatch
  bool resumeRequested = false;
  bool quitRequested = false;
  bool backRequested = false;
  bool toggleFullscreen = false;
  bool toggleVSync = false;

  bool wasCursorLocked = false;

  AudioSystem &audio() { return engine.getSystemManager().getSystem<AudioSystem>(); }
  WindowSystem &window() { return engine.getSystemManager().getSystem<WindowSystem>(); }
  SaveData &save() { return engine.saveData(); }

  // The saved options replace what the game started with (only those that were ever saved)
  void applySavedOptions() {
    if (optionsApplied)
      return;
    optionsApplied = true;
    SaveData &data = save();
    if (data.has(KEY_MASTER))
      audio().setMasterVolume(static_cast<float>(data.getNumber(KEY_MASTER, 1.0)));
    if (data.has(KEY_MUSIC))
      audio().setBusVolume(AudioSystem::Bus::Music, static_cast<float>(data.getNumber(KEY_MUSIC, 1.0)));
    if (data.has(KEY_EFFECTS))
      audio().setBusVolume(AudioSystem::Bus::Effects, static_cast<float>(data.getNumber(KEY_EFFECTS, 1.0)));
    if (data.has(KEY_FULLSCREEN))
      window().setFullscreen(data.getBool(KEY_FULLSCREEN, false));
    if (data.has(KEY_VSYNC))
      engine.setState(Toggle::VSync, data.getBool(KEY_VSYNC, true));
  }

  // The model's view of the engine, as it is now (F6 or the game may have changed it since)
  void readOptions() {
    master = toPercent(audio().getMasterVolume());
    music = toPercent(audio().getBusVolume(AudioSystem::Bus::Music));
    effects = toPercent(audio().getBusVolume(AudioSystem::Bus::Effects));
    fullscreen = window().isFullscreen();
    vsync = engine.getState(Toggle::VSync);
    model.DirtyAllVariables();
  }

  // Applies what the sliders show, and keeps it in the save data
  void applyVolumes() {
    const auto volume = [](int percent) { return static_cast<float>(percent) / 100.0f; };
    if (master != toPercent(audio().getMasterVolume())) {
      audio().setMasterVolume(volume(master));
      save().set(KEY_MASTER, percentToSaved(master));
    }
    if (music != toPercent(audio().getBusVolume(AudioSystem::Bus::Music))) {
      audio().setBusVolume(AudioSystem::Bus::Music, volume(music));
      save().set(KEY_MUSIC, percentToSaved(music));
    }
    if (effects != toPercent(audio().getBusVolume(AudioSystem::Bus::Effects))) {
      audio().setBusVolume(AudioSystem::Bus::Effects, volume(effects));
      save().set(KEY_EFFECTS, percentToSaved(effects));
    }
  }

  bool load() {
    if (loaded)
      return true;
    Rml::Context *context = engine.getUIContext();
    if (!context)
      return false;
    Rml::DataModelConstructor constructor = context->CreateDataModel("engine_pause");
    if (!constructor) {
      LOG_E("[PauseMenu] Could not create the data model");
      return false;
    }
    constructor.Bind("master", &master);
    constructor.Bind("music", &music);
    constructor.Bind("effects", &effects);
    constructor.Bind("fullscreen", &fullscreen);
    constructor.Bind("vsync", &vsync);
    constructor.Bind("options", &options);
    const auto onClick = [](bool &flag) {
      return [&flag](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { flag = true; };
    };
    constructor.BindEventCallback("resume", onClick(resumeRequested));
    constructor.BindEventCallback("quit", onClick(quitRequested));
    constructor.BindEventCallback("back", onClick(backRequested));
    constructor.BindEventCallback("toggle_fullscreen", onClick(toggleFullscreen));
    constructor.BindEventCallback("toggle_vsync", onClick(toggleVSync));
    constructor.BindEventCallback("show_options", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
      options = true;
      model.DirtyVariable("options");
    });
    model = constructor.GetModelHandle();

    document = engine.loadUIDocument(DOCUMENT, false);
    if (!document) {
      context->RemoveDataModel("engine_pause");
      return false;
    }
    loaded = true;
    applySavedOptions();
    return true;
  }

  void setOpen() {
    if (open || !load())
      return;
    open = true;
    // The cursor is the menu's now; the game's lock and pause come back when it closes
    wasCursorLocked = engine.getState(Toggle::CursorLock);
    engine.setState(Toggle::CursorLock, false);
    engine.setState(Toggle::Paused, true);
    options = false;
    resumeRequested = quitRequested = backRequested = toggleFullscreen = toggleVSync = false;
    readOptions();
    document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
    document->PullToFront(); // over the game's own documents, whenever they were loaded
  }

  void setClosed() {
    if (!open)
      return;
    open = false;
    document->Hide();
    engine.setState(Toggle::CursorLock, wasCursorLocked);
    engine.setState(Toggle::Paused, false);
    save().writeIfChanged();
  }

  void showMainScreen() {
    options = false;
    model.DirtyVariable("options");
    save().writeIfChanged();
  }

  void update(bool escapePressed) {
    if (escapePressed) {
      const bool canOpen = enabled && engine.getPlayMode() != Engine::PlayMode::Editing;
      if (open) // Escape steps back: from Options to the first screen, from there out of the menu
        (options ? backRequested : resumeRequested) = true;
      else if (canOpen)
        setOpen();
    }
    if (!open)
      return;

    if (quitRequested) {
      quitRequested = false;
      engine.quit();
    }
    if (toggleFullscreen) {
      toggleFullscreen = false;
      fullscreen = !fullscreen;
      model.DirtyVariable("fullscreen");
      window().setFullscreen(fullscreen);
      save().set(KEY_FULLSCREEN, fullscreen);
    }
    if (toggleVSync) {
      toggleVSync = false;
      vsync = !vsync;
      model.DirtyVariable("vsync");
      engine.setState(Toggle::VSync, vsync);
      save().set(KEY_VSYNC, vsync);
    }
    applyVolumes();
    if (backRequested) {
      backRequested = false;
      showMainScreen();
    }
    if (resumeRequested) {
      resumeRequested = false;
      setClosed();
      return;
    }
    // Stays paused whatever else happened this frame
    engine.setState(Toggle::Paused, true);
  }
};

PauseMenu::PauseMenu(Engine &engine) : m_impl(std::make_unique<Impl>(engine)) {}
PauseMenu::~PauseMenu() = default;

void PauseMenu::setEnabled(bool enabled) {
  m_impl->enabled = enabled;
  if (enabled)
    m_impl->load(); // so the saved options apply from the start, not at the first Escape
}

bool PauseMenu::isEnabled() const { return m_impl->enabled; }
void PauseMenu::open() { m_impl->setOpen(); }
void PauseMenu::close() { m_impl->setClosed(); }
bool PauseMenu::isOpen() const { return m_impl->open; }
void PauseMenu::update(bool escapePressed) { m_impl->update(escapePressed); }
