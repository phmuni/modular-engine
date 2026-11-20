#pragma once
// Scene browser: loads the scene files in assets/scenes and switches between them at runtime with [ and ].

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "systems/inputSystem.h"
#include "systems/sceneSerializer.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

class SceneBrowser : public App {
  std::string m_first;
  std::vector<std::string> m_scenes;
  size_t m_current = 0;

  void refresh(Engine &engine) {
    m_scenes = engine.getSceneSerializer().listScenes(EngineConfig::SCENES_DIR);
  }

  void step(Engine &engine, int direction) {
    refresh(engine); // picks up scenes saved while running
    if (m_scenes.empty())
      return;
    const size_t count = m_scenes.size();
    m_current = (m_current + count + static_cast<size_t>(direction + static_cast<int>(count))) % count;
    engine.loadScene(m_scenes[m_current]);
  }

public:
  explicit SceneBrowser(std::string first = {}) : m_first(std::move(first)) {}

  void setup(Engine &engine) override {
    engine.setState(Toggle::CameraMovement, true); // a viewer: the free camera (Settings) is how scenes are looked at
    refresh(engine);
    if (m_first.empty() && !m_scenes.empty())
      m_first = m_scenes.front();
    if (m_first.empty()) {
      LOG_W("[SceneBrowser] No scenes in %s", EngineConfig::SCENES_DIR);
      return;
    }
    engine.loadScene(m_first);
  }

  void onSceneLoaded(Engine &, const std::string &path) override {
    std::string relative = EngineConfig::relativePath(EngineConfig::resolvePath(path));
    auto it = std::find(m_scenes.begin(), m_scenes.end(), relative);
    if (it != m_scenes.end())
      m_current = static_cast<size_t>(it - m_scenes.begin());
    LOG_I("[SceneBrowser] %s  ([ and ] switch scenes)", relative.c_str());
  }

  void update(Engine &engine, float) override {
    if (engine.input().wasKeyPressed(SDL_SCANCODE_LEFTBRACKET))
      step(engine, -1);
    if (engine.input().wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET))
      step(engine, +1);
  }
};
