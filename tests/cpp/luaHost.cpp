// Runs a Lua test script as the game script, with the `test` helper table: see host.h and tests/lua/test.lua.
//
// The script reports through test.ok/eq/near (which land in test.fail here) and ends with test.done(). An error the
// engine reports for the script (a failed callback, a routine that raised) is a failure too, unless the test announced
// it with test.expectError("part of the message").

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "host.h"
#include "systems/scriptSystem.h"
#include "testing.h"
#include <filesystem>
#include <sol/sol.hpp>
#include <string>
#include <vector>

namespace {
class LuaTest final : public TestHost {
public:
  LuaTest(std::string path, int maxFrames) : TestHost(maxFrames), m_path(std::move(path)) {}

protected:
  void begin(Engine &engine) override {
    sol::state &lua = engine.scripts().lua();
    sol::table test = lua.create_named_table("test");
    test.set_function("fail", [](const std::string &message) { testing::context().fail(message); });
    test.set_function("finish", [this]() { m_done = true; });
    test.set_function("expectError", [this](const std::string &part) { m_expected.push_back({part, false}); });
    test.set_function("frame", [this]() { return frame(); });
    const sol::protected_function_result helper =
        lua.safe_script_file(EngineConfig::resolvePath("../tests/lua/test.lua"), sol::script_pass_on_error);
    if (!helper.valid()) {
      const sol::error error = helper;
      testing::context().fail(std::string("tests/lua/test.lua: ") + error.what());
      m_done = true;
      return;
    }
    // Absolute, so it is found from any folder (a relative path would be taken as relative to assets/)
    if (!engine.scripts().runGameScript(std::filesystem::absolute(m_path).string()) && !engine.scripts().getLastError().empty())
      noticeError(engine);
  }

  bool step(Engine &engine) override {
    noticeError(engine);
    if (!m_done)
      return false;
    for (const Expected &expected : m_expected)
      if (!expected.seen)
        testing::context().fail("test.expectError(\"" + expected.part + "\"): that error never happened");
    return true;
  }

  std::string timeoutMessage() const override {
    return "timed out after " + std::to_string(frame()) + " frames: the script never called test.done()";
  }

private:
  struct Expected {
    std::string part;
    bool seen;
  };
  std::string m_path;
  bool m_done = false;
  std::vector<Expected> m_expected;
  std::string m_lastError;

  // The engine keeps only the latest error: a new one is checked against what the test announced
  void noticeError(Engine &engine) {
    const std::string &error = engine.scripts().getLastError();
    if (error.empty() || error == m_lastError)
      return;
    m_lastError = error;
    for (Expected &expected : m_expected) {
      if (!expected.seen && error.find(expected.part) != std::string::npos) {
        expected.seen = true;
        return;
      }
    }
    testing::context().fail("script error: " + error);
    m_done = true; // the script is broken: what it was going to check will not happen
  }
};
} // namespace

std::unique_ptr<TestHost> makeLuaTest(const std::string &path, int maxFrames) {
  return std::make_unique<LuaTest>(path, maxFrames);
}
