// Game UI system implementation on RmlUi's reference SDL platform and OpenGL 3 renderer backends.

#include "systems/gameUISystem.h"

#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_GL3.h>

namespace {
// Routes RmlUi's own warnings and errors into the engine log
class LoggingSystemInterface final : public SystemInterface_SDL {
public:
  using SystemInterface_SDL::SystemInterface_SDL;

  // The engine's time rather than the wall clock, so animations follow fixed-step runs too
  double GetElapsedTime() override { return clock; }
  double clock = 0.0;

  bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
    switch (type) {
    case Rml::Log::LT_ERROR:
    case Rml::Log::LT_ASSERT:
      LOG_E("[GameUI] %s", message.c_str());
      break;
    case Rml::Log::LT_WARNING:
      LOG_W("[GameUI] %s", message.c_str());
      break;
    default:
      LOG_D("[GameUI] %s", message.c_str());
      break;
    }
    return true;
  }
};

bool isMouseEvent(Uint32 type) {
  return type == SDL_EVENT_MOUSE_MOTION || type == SDL_EVENT_MOUSE_BUTTON_DOWN || type == SDL_EVENT_MOUSE_BUTTON_UP ||
         type == SDL_EVENT_MOUSE_WHEEL;
}

bool isKeyEvent(Uint32 type) {
  return type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP || type == SDL_EVENT_TEXT_INPUT;
}
} // namespace

struct GameUISystem::Impl {
  SDL_Window *window = nullptr;
  std::unique_ptr<LoggingSystemInterface> system;
  std::unique_ptr<RenderInterface_GL3> renderer;
  Rml::Context *context = nullptr;
  bool debuggerVisible = false;
  ViewRect view;
};

GameUISystem::GameUISystem(SDL_Window *window) : m_impl(std::make_unique<Impl>()) {
  auto &impl = *m_impl;
  impl.window = window;
  impl.renderer = std::make_unique<RenderInterface_GL3>();
  if (!*impl.renderer) {
    LOG_E("[GameUI] Could not create the RmlUi renderer; game UI disabled");
    impl.renderer.reset();
    return;
  }
  impl.system = std::make_unique<LoggingSystemInterface>(window);

  Rml::SetSystemInterface(impl.system.get());
  Rml::SetRenderInterface(impl.renderer.get());
  Rml::Initialise();

  int width = 0, height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);
  impl.context = Rml::CreateContext("game", Rml::Vector2i(width, height));
  if (!impl.context) {
    LOG_E("[GameUI] Could not create the RmlUi context; game UI disabled");
    shutdown();
    return;
  }
  // dp units follow the display's scale: the pixel density on a Retina screen, the content scale where window
  // units are pixels (Windows at 150 %). 1 for the same pictures anywhere (deterministic runs).
  const float displayScale = SDL_GetWindowDisplayScale(window);
  impl.context->SetDensityIndependentPixelRatio(displayScale > 0.0f ? displayScale : SDL_GetWindowPixelDensity(window));
  Rml::Debugger::Initialise(impl.context);
  // The debugger leaves a document of no size, always shown, at the context's corner: with the pointer exactly there
  // it would be what is hovered, and every click would count as the UI's
  if (Rml::ElementDocument *hook = impl.context->GetDocument("rmlui-debug-hook"))
    hook->SetProperty("pointer-events", "none");

  for (const char *font : EngineConfig::UI_FONTS)
    loadFont(font);
}

GameUISystem::~GameUISystem() { shutdown(); }

void GameUISystem::shutdown() {
  auto &impl = *m_impl;
  if (!impl.renderer)
    return;
  Rml::Shutdown(); // releases documents, fonts and textures through the renderer
  impl.context = nullptr;
  impl.renderer.reset();
  impl.system.reset();
}

bool GameUISystem::isAvailable() const { return m_impl->context != nullptr; }

Rml::Context *GameUISystem::getContext() const { return m_impl->context; }

bool GameUISystem::loadFont(const std::string &path, bool fallback) {
  if (!isAvailable())
    return false;
  std::string resolved = EngineConfig::resolveAsset(path);
  if (!Rml::LoadFontFace(resolved, fallback)) {
    LOG_W("[GameUI] Could not load font %s", resolved.c_str());
    return false;
  }
  return true;
}

Rml::ElementDocument *GameUISystem::loadDocument(const std::string &path, bool show) {
  if (!isAvailable())
    return nullptr;
  std::string resolved = EngineConfig::resolveAsset(path);
  Rml::ElementDocument *document = m_impl->context->LoadDocument(resolved);
  if (!document) {
    LOG_W("[GameUI] Could not load document %s", resolved.c_str());
    return nullptr;
  }
  if (show)
    document->Show();
  return document;
}

bool GameUISystem::processEvent(SDL_Event &event, bool mouseAllowed, bool keysAllowed) {
  auto &impl = *m_impl;
  if (!impl.context)
    return false;
  if (isMouseEvent(event.type) && !mouseAllowed) {
    // Clear the hover state once, so nothing stays highlighted under a locked or captured cursor
    if (event.type == SDL_EVENT_MOUSE_MOTION)
      impl.context->ProcessMouseLeave();
    return false;
  }
  if (isKeyEvent(event.type) && !keysAllowed)
    return false;
  // Resizes are applied by the handler (SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED); returns false when the UI used the event
  if (impl.view.active && event.type == SDL_EVENT_MOUSE_MOTION) {
    // The panel's corner is the UI's origin (the event is in window units, the view in pixels)
    SDL_Event moved = event;
    const float density = std::max(SDL_GetWindowPixelDensity(impl.window), 1.0f);
    moved.motion.x -= static_cast<float>(impl.view.x) / density;
    moved.motion.y -= static_cast<float>(impl.view.y) / density;
    return !RmlSDL::InputEventHandler(impl.context, impl.window, moved);
  }
  return !RmlSDL::InputEventHandler(impl.context, impl.window, event);
}

void GameUISystem::update(bool showDebugger, float seconds) {
  auto &impl = *m_impl;
  if (!impl.context)
    return;
  impl.system->clock += seconds;
  if (showDebugger != impl.debuggerVisible) {
    impl.debuggerVisible = showDebugger;
    Rml::Debugger::SetVisible(showDebugger);
  }
  impl.context->Update();
}

void GameUISystem::setView(const ViewRect &view) {
  auto &impl = *m_impl;
  impl.view = view;
  if (!impl.context)
    return;
  int width = view.width, height = view.height;
  if (!view.active)
    SDL_GetWindowSizeInPixels(impl.window, &width, &height);
  if (impl.context->GetDimensions() != Rml::Vector2i(width, height))
    impl.context->SetDimensions(Rml::Vector2i(width, height));
}

void GameUISystem::render() {
  auto &impl = *m_impl;
  if (!impl.context)
    return;
  int width = 0, height = 0, windowHeight = 0;
  SDL_GetWindowSizeInPixels(impl.window, &width, &windowHeight);
  height = windowHeight;
  if (impl.view.active) {
    width = impl.view.width;
    height = impl.view.height;
  }
  if (impl.context->GetDimensions() != Rml::Vector2i(width, height))
    impl.context->SetDimensions(Rml::Vector2i(width, height));

  // Nothing on screen (no document shown, e.g. no game UI and the debugger closed): skip the frame. The GL3 backend
  // otherwise still clears its own multisampled layer and blits it over the scene, and that blit makes the driver
  // submit the frame's GPU work early: about 1 ms a frame on macOS, for nothing. The debugger's hook document is
  // always "visible" but draws nothing, so it does not count.
  bool anyVisible = false;
  for (int i = 0; i < impl.context->GetNumDocuments() && !anyVisible; ++i) {
    Rml::ElementDocument *document = impl.context->GetDocument(i);
    anyVisible = document->IsVisible() && document->GetId() != "rmlui-debug-hook";
  }
  if (!anyVisible)
    return;

  // The renderer saves the engine's GL state here and restores it in EndFrame
  if (impl.view.active)
    impl.renderer->SetViewport(width, height, impl.view.x, windowHeight - impl.view.y - impl.view.height);
  else
    impl.renderer->SetViewport(width, height);
  impl.renderer->BeginFrame();
  impl.context->Render();
  impl.renderer->EndFrame();
}
