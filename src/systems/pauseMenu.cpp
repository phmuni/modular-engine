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
#include <cstdio>
#include <string>
#include <vector>

namespace {
constexpr const char *DOCUMENT = "../assets/ui/engine/pauseMenu.rml";

// Where the options live in the save data
constexpr const char *KEY_MASTER = "engine.volume.master";
constexpr const char *KEY_MUSIC = "engine.volume.music";
constexpr const char *KEY_EFFECTS = "engine.volume.effects";
constexpr const char *KEY_FULLSCREEN = "engine.fullscreen";
constexpr const char *KEY_VSYNC = "engine.vsync";
// The window's size is kept under the same keys the engine remembers where the window was closed (Engine::startWindow)
constexpr const char *KEY_WINDOW_WIDTH = "engine.window.width";
constexpr const char *KEY_WINDOW_HEIGHT = "engine.window.height";
constexpr const char *KEY_RENDER_SCALE = "engine.renderScale";
constexpr const char *KEY_MSAA = "engine.msaa";
constexpr const char *KEY_FXAA = "engine.fxaa";
constexpr const char *KEY_FSR = "engine.fsr";
constexpr const char *KEY_FSR_SHARPNESS = "engine.fsrSharpness";

int toPercent(float volume) { return std::clamp(static_cast<int>(std::lround(volume * 100.0f)), 0, 100); }
// 0.49 in the file, not the 0.49000000953674316 a float widens to
double percentToSaved(int percent) { return static_cast<double>(percent) / 100.0; }

// The render scale slider moves in steps of 5 %, as the document's step says
int scaleToSlider(float scale) { return std::clamp(static_cast<int>(std::lround(scale * 20.0f)) * 5, 50, 200); }

// FSR's sharpness slider is in hundredths of a stop (0 to 200, steps of 5)
int sharpnessToSlider(float stops) { return std::clamp(static_cast<int>(std::lround(stops * 20.0f)) * 5, 0, 200); }
Rml::String sharpnessText(int slider) {
  char text[16];
  std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(slider) / 100.0);
  return text;
}
Rml::String msaaName(int samples) { return samples >= 2 ? "MSAA " + std::to_string(samples) + "x" : "Off"; }

std::string sizeText(const glm::ivec2 &size) { return std::to_string(size.x) + " x " + std::to_string(size.y); }
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
  int renderScale = 100; // percent of the framebuffer's pixels the scene is drawn at; the slider is 50 to 200
  Rml::String resolution;       // the window's size, as shown
  Rml::String renderResolution; // the scene's size in pixels, as shown
  // Antialiasing and upscaling (one block: the rows in pauseMenu.rml, and the same-named keys in the save data)
  Rml::String msaaText = "Off";
  bool fxaa = false;
  bool fsr = false;
  int fsrSharpness = 20; // hundredths of a stop
  Rml::String fsrSharpnessText = "0.20";
  int stepMsaa = 0; // -1 or 1: the previous or next MSAA level
  bool toggleFxaa = false;
  bool toggleFsr = false;
  bool options = false;         // the Options screen instead of the main one

  // What buttons asked for: handled in update(), outside RmlUi's event dispatch
  bool resumeRequested = false;
  bool quitRequested = false;
  bool backRequested = false;
  bool toggleFullscreen = false;
  bool toggleVSync = false;
  int stepResolution = 0; // -1 or 1: the previous or next window size

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
    // The window's own size comes back with its place when the window is shown (Engine::startWindow)
    if (data.has(KEY_RENDER_SCALE))
      engine.setRenderScale(static_cast<float>(data.getNumber(KEY_RENDER_SCALE, 1.0)));
    if (data.has(KEY_MSAA))
      engine.setMsaa(static_cast<int>(data.getNumber(KEY_MSAA, engine.msaa())));
    if (data.has(KEY_FXAA))
      engine.setFxaa(data.getBool(KEY_FXAA, false));
    if (data.has(KEY_FSR))
      engine.setFsr(data.getBool(KEY_FSR, false));
    if (data.has(KEY_FSR_SHARPNESS))
      engine.setFsrSharpness(static_cast<float>(data.getNumber(KEY_FSR_SHARPNESS, engine.fsrSharpness())));
  }

  // The model's view of the engine, as it is now (F6 or the game may have changed it since)
  void readOptions() {
    master = toPercent(audio().getMasterVolume());
    music = toPercent(audio().getBusVolume(AudioSystem::Bus::Music));
    effects = toPercent(audio().getBusVolume(AudioSystem::Bus::Effects));
    fullscreen = window().isFullscreen();
    vsync = engine.getState(Toggle::VSync);
    renderScale = scaleToSlider(engine.renderScale());
    readAntialiasing();
    refreshResolutions();
    model.DirtyAllVariables();
  }

  void readAntialiasing() {
    msaaText = msaaName(engine.msaa());
    fxaa = engine.fxaa();
    fsr = engine.fsr();
    fsrSharpness = sharpnessToSlider(engine.fsrSharpness());
    fsrSharpnessText = sharpnessText(fsrSharpness);
  }

  // Applies the antialiasing rows' requests (the MSAA step, the FXAA and FSR switches, the sharpness slider) and keeps
  // them in the save data
  void applyAntialiasing() {
    if (stepMsaa != 0) {
      const int direction = stepMsaa;
      stepMsaa = 0;
      static const int levels[] = {0, 2, 4, 8};
      const int before = engine.msaa();
      int index = 0;
      for (int i = 0; i < 4; ++i)
        if (levels[i] == before)
          index = i;
      index = (index + direction + 4) % 4;
      engine.setMsaa(levels[index]);
      if (engine.msaa() == before) // the GPU does not go that high: around to Off (or to the top, going down)
        engine.setMsaa(direction > 0 ? 0 : 8);
      save().set(KEY_MSAA, engine.msaa());
    }
    if (toggleFxaa) {
      toggleFxaa = false;
      engine.setFxaa(!engine.fxaa());
      save().set(KEY_FXAA, engine.fxaa());
    }
    if (toggleFsr) {
      toggleFsr = false;
      engine.setFsr(!engine.fsr());
      save().set(KEY_FSR, engine.fsr());
    }
    if (fsrSharpness != sharpnessToSlider(engine.fsrSharpness())) {
      engine.setFsrSharpness(static_cast<float>(fsrSharpness) / 100.0f);
      save().set(KEY_FSR_SHARPNESS, percentToSaved(fsrSharpness));
    }
    const Rml::String text = msaaName(engine.msaa());
    if (text != msaaText || fxaa != engine.fxaa() || fsr != engine.fsr() ||
        sharpnessText(fsrSharpness) != fsrSharpnessText) {
      msaaText = text;
      fxaa = engine.fxaa();
      fsr = engine.fsr();
      fsrSharpnessText = sharpnessText(fsrSharpness);
      model.DirtyVariable("msaaText");
      model.DirtyVariable("fxaa");
      model.DirtyVariable("fsr");
      model.DirtyVariable("fsrSharpnessText");
    }
  }

  // The texts that follow the window's and the scene's sizes, which change by themselves (dragging the window)
  void refreshResolutions() {
    resolution = sizeText(engine.windowSize());
    renderResolution = sizeText(engine.renderResolution());
  }

  // The window moves to the next (or previous) of the sizes that fit the display, from where it is now: a size
  // that is not on the list (the window was dragged) goes to the nearest one in that direction
  void applyResolutionStep() {
    if (stepResolution == 0)
      return;
    const int direction = stepResolution;
    stepResolution = 0;
    if (fullscreen) // the button looks disabled; a click that got in anyway does nothing
      return;
    const std::vector<glm::ivec2> sizes = engine.resolutions();
    const glm::ivec2 current = engine.windowSize();
    const auto area = [](const glm::ivec2 &size) { return static_cast<long long>(size.x) * size.y; };
    const glm::ivec2 *chosen = nullptr;
    for (const glm::ivec2 &size : sizes) { // smallest first
      if (direction > 0 && area(size) > area(current)) {
        chosen = &size;
        break;
      }
      if (direction < 0 && area(size) < area(current))
        chosen = &size; // the last one that is smaller
    }
    if (!chosen) // past the end: around to the other one
      chosen = direction > 0 ? &sizes.front() : &sizes.back();
    engine.setWindowSize(chosen->x, chosen->y);
    const glm::ivec2 applied = engine.windowSize();
    save().set(KEY_WINDOW_WIDTH, applied.x);
    save().set(KEY_WINDOW_HEIGHT, applied.y);
  }

  // Applies what the render scale slider shows, and keeps it in the save data
  void applyRenderScale() {
    if (renderScale != scaleToSlider(engine.renderScale())) {
      engine.setRenderScale(static_cast<float>(renderScale) / 100.0f);
      save().set(KEY_RENDER_SCALE, percentToSaved(renderScale));
    }
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
    constructor.Bind("renderScale", &renderScale);
    constructor.Bind("resolution", &resolution);
    constructor.Bind("renderResolution", &renderResolution);
    constructor.Bind("options", &options);
    constructor.Bind("msaaText", &msaaText);
    constructor.Bind("fxaa", &fxaa);
    constructor.Bind("fsr", &fsr);
    constructor.Bind("fsrSharpness", &fsrSharpness);
    constructor.Bind("fsrSharpnessText", &fsrSharpnessText);
    constructor.BindEventCallback("aa_prev", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
      stepMsaa = -1;
    });
    constructor.BindEventCallback("aa_next", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
      stepMsaa = 1;
    });
    constructor.BindEventCallback("toggle_fxaa", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
      toggleFxaa = true;
    });
    constructor.BindEventCallback("toggle_fsr", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
      toggleFsr = true;
    });
    const auto onClick = [](bool &flag) {
      return [&flag](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { flag = true; };
    };
    constructor.BindEventCallback("resume", onClick(resumeRequested));
    constructor.BindEventCallback("quit", onClick(quitRequested));
    constructor.BindEventCallback("back", onClick(backRequested));
    constructor.BindEventCallback("toggle_fullscreen", onClick(toggleFullscreen));
    constructor.BindEventCallback("toggle_vsync", onClick(toggleVSync));
    const auto onStep = [this](int direction) {
      return [this, direction](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        stepResolution = direction;
      };
    };
    constructor.BindEventCallback("resolution_prev", onStep(-1));
    constructor.BindEventCallback("resolution_next", onStep(1));
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
    stepResolution = 0;
    stepMsaa = 0;
    toggleFxaa = toggleFsr = false;
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
    applyResolutionStep();
    applyRenderScale();
    applyAntialiasing();
    // The sizes can change without a click: the window was dragged, or the menu's own request just landed
    const Rml::String windowText = sizeText(engine.windowSize());
    const Rml::String sceneText = sizeText(engine.renderResolution());
    if (windowText != resolution || sceneText != renderResolution) {
      resolution = windowText;
      renderResolution = sceneText;
      model.DirtyVariable("resolution");
      model.DirtyVariable("renderResolution");
    }
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
void PauseMenu::setCursorLockOnClose(bool locked) { m_impl->wasCursorLocked = locked; }
void PauseMenu::update(bool escapePressed) { m_impl->update(escapePressed); }
