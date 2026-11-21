// The editor's scene list: the filter's terms (name, t:, tag:) on their own, and the panel drawn on a scene with a
// family, a single-entity prefab instance and a few hundred plain entities, with and without a search.

#include "components/prefabInstance.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "systems/editor/sceneFilter.h"
#include "systems/editorUISystem.h"
#include "systems/sceneSerializer.h"
#include "systems/stateSystem.h"
#include "testing.h"
#include <cstdlib>

namespace {
bool fits(const std::string &search, const std::string &name, const std::vector<std::string> &components,
          const std::vector<std::string> &tags) {
  auto has = [](const std::vector<std::string> &list, const std::string &term) {
    for (const std::string &item : list)
      if (sceneFilter::lower(item) == term)
        return true;
    return false;
  };
  return sceneFilter::matches(
      sceneFilter::parse(search), name, [&](const std::string &term) { return has(components, term); },
      [&](const std::string &term) { return has(tags, term); });
}
} // namespace

TEST_CASE(scene_filter_terms) {
  CHECK(sceneFilter::parse("").empty());
  CHECK(sceneFilter::parse("  t:  ").empty()); // a term still being typed
  CHECK(fits("", "Anything", {}, {}));
  CHECK(fits("wheel", "Car_Wheel_FL", {}, {}));
  CHECK(fits("WHEEL fl", "Car_Wheel_FL", {}, {}));
  CHECK(!fits("wheel rr", "Car_Wheel_FL", {}, {}));
  CHECK(fits("t:light", "Lamp", {"Light", "Transform"}, {}));
  CHECK(fits("T:Light", "Lamp", {"Light"}, {}));
  CHECK(!fits("t:model", "Lamp", {"Light"}, {}));
  CHECK(fits("tag:Enemy", "Orc", {}, {"enemy"}));
  CHECK(!fits("tag:ally", "Orc", {}, {"enemy"}));
  CHECK(fits("orc t:model tag:enemy", "Orc", {"Model"}, {"enemy"}));
  CHECK(!fits("orc t:model tag:enemy", "Orc", {"Light"}, {"enemy"}));
}

TEST_CASE(scene_list_draws) {
  const std::string file = testing::tempDir("scene_list") + "/lamp.json";
  GameEntity prefab = engine.create("Lamp", Desc("sphere"));
  CHECK(engine.getSceneSerializer().applyInstance(prefab.id(), file));
  prefab.destroy();

  GameEntity car = engine.create("Car", Desc("box"));
  GameEntity wheel = engine.create("Wheel_FL", Desc("sphere").at(1, 0, 0).tags({"enemy"}));
  CHECK(wheel.setParent(car));
  for (int i = 0; i < 300; ++i)
    engine.create("Crate " + std::to_string(i), Desc("box").at(float(i), 0, 5));

  auto &editor = engine.getSystemManager().getSystem<EditorUISystem>();
  engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, true);
  editor.selectedEntity = wheel.id();
  steps.step(1, [&engine, file]() {
    const auto placed = engine.getSceneSerializer().instantiate(file);
    CHECK_EQ(placed.size(), size_t(1)); // a single-entity instance
    CHECK(engine.has<PrefabInstance>(placed[0]));
    if (std::getenv("MODULAR_TEST_SCREENSHOT"))
      engine.setWindowSize(1500, 1300);
  });
  steps.step(6, [&engine]() {
    if (const char *path = std::getenv("MODULAR_TEST_SCREENSHOT"))
      engine.captureScreenshot(path, 0.0f, false);
  });
  steps.step(3, [&editor]() { editor.setSceneFilter("t:Model wheel"); });
  steps.step(4, [&editor]() { editor.setSceneFilter("tag:enemy"); });
  steps.step(3, [&editor]() { editor.setSceneFilter("crate 29"); });
  steps.step(3, [&editor]() { editor.setSceneFilter("nothing matches this"); });
  steps.step(3, [&editor]() { editor.setSceneFilter(""); });
  steps.step(3, [&engine]() { engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, false); });
}
