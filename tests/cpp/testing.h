#pragma once
// A small test harness for bin/engine_tests: registered test cases, assertions that record a failure and go on, and
// Steps to run checks on later frames (the engine owns the loop, so a test cannot just wait). Each test runs in its own
// process, since the engine opens a window and a GL context and is not meant to be started twice.

#include <cmath>
#include <cstddef>
#include <functional>
#include <glm/glm.hpp>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class Engine;
class SaveData;

namespace testing {

// The failures of the test being run
struct Context {
  std::vector<std::string> failures;
  void fail(const std::string &message);
};
Context &context();

// Checks to run on later frames, in order: step(n, fn) runs fn n frames after the previous step ran (0: in the same
// frame). The test is over when the last one has run. State between steps lives in captured shared_ptrs.
class Steps {
public:
  void step(int frames, std::function<void()> action) { m_steps.push_back({frames, std::move(action), {}, {}}); }
  // The same in seconds of game time at the fixed 60 frames per second
  void seconds(float seconds, std::function<void()> action) {
    step(static_cast<int>(std::lround(seconds * 60.0f)), std::move(action));
  }
  // Waits until `condition` holds (checked each frame, from the frame after the previous step), then runs `action`; a
  // failure if that takes more than `maxFrames`
  void until(std::function<bool()> condition, std::string description, int maxFrames, std::function<void()> action = {}) {
    m_steps.push_back({maxFrames, std::move(action), std::move(condition), std::move(description)});
  }
  // Runs the steps that are due this frame; true once there are none left
  bool tick();

private:
  struct Step {
    int frames;
    std::function<void()> action;
    std::function<bool()> condition; // set: wait for it instead of for `frames`
    std::string description;
  };
  std::vector<Step> m_steps;
  size_t m_next = 0;
  int m_waited = 0;
};

using TestFunction = void (*)(Engine &, Steps &);
struct TestCase {
  const char *name;
  TestFunction function;
};
std::vector<TestCase> &registry();
struct Registrar {
  Registrar(const char *name, TestFunction function) { registry().push_back({name, function}); }
};

// What a test needs in place before the engine starts the game (Engine::run): the player's saved choices, and the
// game's own defaults. STARTUP(name) { ... } runs for the test of that name, with `engine` and `save` as parameters.
using StartupFunction = void (*)(Engine &, SaveData &);
std::vector<std::pair<std::string, StartupFunction>> &startups();
struct StartupRegistrar {
  StartupRegistrar(const char *name, StartupFunction function) { startups().push_back({name, function}); }
};

inline std::string show(const std::string &value) { return "\"" + value + "\""; }
inline std::string show(const char *value) { return show(std::string(value)); }
inline std::string show(bool value) { return value ? "true" : "false"; }
inline std::string show(const glm::vec3 &v) {
  std::ostringstream out;
  out << "vec3(" << v.x << ", " << v.y << ", " << v.z << ")";
  return out.str();
}
inline std::string show(const glm::vec2 &v) {
  std::ostringstream out;
  out << "vec2(" << v.x << ", " << v.y << ")";
  return out.str();
}
template <typename T> std::string show(const T &value) {
  std::ostringstream out;
  if constexpr (std::is_enum_v<T>)
    out << static_cast<long long>(value); // enum classes have no <<
  else
    out << value;
  return out.str();
}

inline bool isNear(float a, float b, float epsilon) { return std::fabs(a - b) <= epsilon; }
inline bool isNear(const glm::vec2 &a, const glm::vec2 &b, float epsilon) {
  return isNear(a.x, b.x, epsilon) && isNear(a.y, b.y, epsilon);
}
inline bool isNear(const glm::vec3 &a, const glm::vec3 &b, float epsilon) {
  return isNear(a.x, b.x, epsilon) && isNear(a.y, b.y, epsilon) && isNear(a.z, b.z, epsilon);
}

void fail(const char *file, int line, const std::string &message);

// A new empty folder under the system's temporary one, named for this test, removed when the process ends. Tests that
// write files (scenes, saved data) write here, never to the game's own folders.
std::string tempDir(const std::string &name);

// Writes every entity to a scene file, the ones the test created as the game would included. Engine::saveScene leaves
// those out (they are the game's, see Engine::isRuntime), and the tests that check what scene files hold build their
// scenes in code.
bool saveWholeScene(Engine &engine, const std::string &path);

} // namespace testing

// A test case: TEST_CASE(name) { ...runs in setup; use steps.step(...) for what comes later... }. `engine` and `steps`
// are the parameters.
#define TEST_CASE(name)                                                                                              \
  static void name(Engine &engine, testing::Steps &steps);                                                           \
  static testing::Registrar registrar_##name(#name, name);                                                           \
  static void name([[maybe_unused]] Engine &engine, [[maybe_unused]] testing::Steps &steps)

#define STARTUP(name)                                                                                                \
  static void startup_##name(Engine &, SaveData &);                                                                  \
  static testing::StartupRegistrar startupRegistrar_##name(#name, startup_##name);                                   \
  static void startup_##name([[maybe_unused]] Engine &engine, [[maybe_unused]] SaveData &save)

#define CHECK(condition)                                                                                             \
  do {                                                                                                               \
    if (!(condition))                                                                                                \
      testing::fail(__FILE__, __LINE__, "CHECK(" #condition ") is false");                                          \
  } while (0)

#define CHECK_EQ(actual, expected)                                                                                   \
  do {                                                                                                               \
    const auto &actualValue = (actual);                                                                              \
    const auto &expectedValue = (expected);                                                                          \
    if (!(actualValue == expectedValue))                                                                             \
      testing::fail(__FILE__, __LINE__,                                                                              \
                    #actual " is " + testing::show(actualValue) + ", expected " + testing::show(expectedValue));     \
  } while (0)

// Floats and vec3s, within `epsilon`
#define CHECK_NEAR(actual, expected, epsilon)                                                                        \
  do {                                                                                                               \
    const auto actualValue = (actual);                                                                               \
    const auto expectedValue = (expected);                                                                           \
    if (!testing::isNear(actualValue, expectedValue, (epsilon)))                                                       \
      testing::fail(__FILE__, __LINE__,                                                                              \
                    #actual " is " + testing::show(actualValue) + ", expected " + testing::show(expectedValue) +     \
                        " (within " + testing::show(epsilon) + ")");                                                 \
  } while (0)
