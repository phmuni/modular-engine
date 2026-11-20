// The loading screen: see loadingScreen.h.

#include "systems/loadingScreen.h"

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <filesystem>

namespace {
constexpr const char *MODEL = "loading";
constexpr const char *DEFAULT_DOCUMENT = "ui/loading.rml";
} // namespace

struct LoadingScreen::Impl {
  explicit Impl(Engine &e) : engine(e) {}

  enum class State { Idle, Waiting, Shown };

  Engine &engine;
  LoadingScreenOptions options;
  bool enabled = false;
  Rml::ElementDocument *document = nullptr;
  Rml::DataModelHandle model;
  float progress = 0.0f;
  int percent = 0;
  State state = State::Idle;
  float time = 0.0f; // waiting for the delay, then showing for the minimum time

  void unload() {
    hide();
    if (!document)
      return;
    if (Rml::Context *context = engine.getUIContext()) {
      context->UnloadDocument(document);
      context->RemoveDataModel(MODEL);
    }
    document = nullptr;
  }

  bool load() {
    if (document)
      return true;
    Rml::Context *context = engine.getUIContext();
    if (!context)
      return false;
    Rml::DataModelConstructor constructor = context->CreateDataModel(MODEL);
    if (!constructor) {
      LOG_E("[LoadingScreen] Could not create the data model");
      return false;
    }
    constructor.Bind("progress", &progress);
    constructor.Bind("percent", &percent);
    model = constructor.GetModelHandle();
    document = engine.loadUIDocument(options.document, false);
    if (!document) {
      context->RemoveDataModel(MODEL);
      return false;
    }
    return true;
  }

  void setProgress(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    const int whole = static_cast<int>(value * 100.0f + 0.5f);
    if (value != progress || whole != percent) {
      progress = value;
      percent = whole;
      model.DirtyVariable("progress");
      model.DirtyVariable("percent");
    }
  }

  void show() {
    if (!document)
      return;
    document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    document->PullToFront(); // over the game's menus, whenever they were loaded
  }

  void hide() {
    if (document && state == State::Shown)
      document->Hide();
    state = State::Idle;
  }
};

LoadingScreen::LoadingScreen(Engine &engine) : m_impl(std::make_unique<Impl>(engine)) {}
LoadingScreen::~LoadingScreen() = default;

void LoadingScreen::configure(const LoadingScreenOptions &options) {
  Impl &impl = *m_impl;
  impl.unload();
  impl.options = options;
  if (impl.options.document.empty())
    impl.options.document = DEFAULT_DOCUMENT;
  std::error_code error;
  if (!std::filesystem::exists(EngineConfig::resolveAsset(impl.options.document), error)) {
    LOG_W("[LoadingScreen] The document %s does not exist; no loading screen", impl.options.document.c_str());
    impl.enabled = false;
    return;
  }
  impl.enabled = impl.load();
}

void LoadingScreen::disable() {
  m_impl->unload();
  m_impl->enabled = false;
}

bool LoadingScreen::isEnabled() const { return m_impl->enabled; }
bool LoadingScreen::isShown() const { return m_impl->state == Impl::State::Shown; }
const LoadingScreenOptions &LoadingScreen::options() const { return m_impl->options; }

void LoadingScreen::update(float deltaTime, bool loading, float progress) {
  Impl &impl = *m_impl;
  if (!impl.enabled)
    return;
  if (impl.state == Impl::State::Idle && loading) {
    impl.state = Impl::State::Waiting;
    impl.time = 0.0f;
  }
  if (impl.state == Impl::State::Waiting) {
    if (!loading) {
      impl.state = Impl::State::Idle; // over before the screen was due
      return;
    }
    impl.time += deltaTime;
    if (impl.time >= impl.options.delay) {
      impl.state = Impl::State::Shown;
      impl.time = 0.0f;
      impl.setProgress(progress);
      impl.show();
    }
    return;
  }
  if (impl.state == Impl::State::Shown) {
    impl.time += deltaTime;
    impl.setProgress(loading ? progress : 1.0f);
    if (!loading && impl.time >= impl.options.minimumTime)
      impl.hide();
  }
}

void LoadingScreen::stop() {
  Impl &impl = *m_impl;
  impl.hide();
  impl.setProgress(0.0f);
}
