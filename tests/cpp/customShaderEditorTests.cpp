// The editor's Material section for custom shaders: drawing the inspector of a part that has one (and one whose file does
// not compile) runs the code that reads the file's parameters into widgets.

#include "describe.h"
#include "foundation/core/engine.h"
#include "systems/editorUISystem.h"
#include "systems/stateSystem.h"
#include "testing.h"
#include <cstdlib>
#include <fstream>

TEST_CASE(custom_shader_inspector_draws) {
  const std::string broken = testing::tempDir("shader_inspector") + "/broken.glsl";
  std::ofstream(broken) << "void surface(inout Surface s) { oops }\n";

  GameEntity good = engine.create("Good", Desc("box"));
  good.setShader("shaders/examples/water.glsl");
  good.setShaderParam("waveHeight", 0.3f);
  GameEntity bad = engine.create("Bad", Desc("box").at(3, 0, 0));
  bad.setShader(broken);

  auto &editor = engine.getSystemManager().getSystem<EditorUISystem>();
  engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, true);
  editor.selectedEntity = good.id();
  // Written for looking at the panel by hand: MODULAR_TEST_SCREENSHOT=/path/to/file.bmp
  steps.step(1, [&engine]() {
    if (std::getenv("MODULAR_TEST_SCREENSHOT"))
      engine.setWindowSize(1500, 1300);
  });
  steps.step(8, [&engine]() {
    if (const char *path = std::getenv("MODULAR_TEST_SCREENSHOT"))
      engine.captureScreenshot(path, 0.0f, false);
  });
  steps.step(3, [&editor, bad]() { editor.selectedEntity = bad.id(); });
  steps.step(3, [&engine]() { engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, false); });
}
