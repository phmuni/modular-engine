// The harness's state: see testing.h.

#include "testing.h"
#include "foundation/core/engine.h"
#include "systems/sceneSerializer.h"
#include <filesystem>
#include <random>
#include <system_error>

namespace testing {

Context &context() {
  static Context instance;
  return instance;
}

void Context::fail(const std::string &message) { failures.push_back(message); }

std::vector<TestCase> &registry() {
  static std::vector<TestCase> cases; // filled by static registrars, so created on first use
  return cases;
}

std::vector<std::pair<std::string, StartupFunction>> &startups() {
  static std::vector<std::pair<std::string, StartupFunction>> functions;
  return functions;
}

void fail(const char *file, int line, const std::string &message) {
  context().fail(std::string(file) + ":" + std::to_string(line) + ": " + message);
}

bool Steps::tick() {
  while (m_next < m_steps.size()) {
    const Step &step = m_steps[m_next];
    if (step.condition) {
      if (!step.condition()) {
        if (++m_waited <= step.frames)
          return false;
        fail("(steps)", 0, "timed out after " + std::to_string(step.frames) + " frames waiting for: " + step.description);
      }
    } else if (m_waited < step.frames) {
      ++m_waited;
      return false;
    }
    m_waited = 0;
    // Copied first: the action may add steps, which can reallocate the list
    const std::function<void()> action = step.action;
    ++m_next;
    if (action)
      action();
  }
  return true;
}

namespace {
// Removes the folders tempDir made, when the process ends
struct TempFolders {
  std::vector<std::filesystem::path> paths;
  ~TempFolders() {
    std::error_code ignored;
    for (const std::filesystem::path &path : paths)
      std::filesystem::remove_all(path, ignored);
  }
};
} // namespace

bool saveWholeScene(Engine &engine, const std::string &path) {
  return engine.getSceneSerializer().save(path, true);
}

std::string tempDir(const std::string &name) {
  static TempFolders folders;
  static int counter = 0;
  std::error_code error;
  // Random, so tests running at the same time (or a crashed run's leftovers) never share a folder
  const std::filesystem::path path =
      std::filesystem::temp_directory_path(error) /
      ("modular-engine-test-" + name + "-" + std::to_string(std::random_device{}()) + "-" +
       std::to_string(++counter));
  std::filesystem::create_directories(path, error);
  folders.paths.push_back(path);
  return path.generic_string();
}

} // namespace testing
