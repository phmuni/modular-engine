// The project launcher: creates game projects (Lua, C++ or both) from the engine's templates, builds and plays them,
// and opens them in VS Code or the file browser. The same as tools/new_game.py, with a window.
//
// It lives in the engine's bin/launcher; the engine checkout (its templates and its prebuilt Lua runner) is two
// folders up. The project list is kept in the user's preferences folder.

#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_sdl3.h>
#include <imgui/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace {

enum class ProjectType { Lua, Cpp, Mixed };
const char *typeName(ProjectType type) {
  return type == ProjectType::Lua ? "Lua" : type == ProjectType::Cpp ? "C++" : "C++ + Lua";
}
const char *typeKey(ProjectType type) { return type == ProjectType::Lua ? "lua" : type == ProjectType::Cpp ? "cpp" : "mixed"; }
ProjectType typeFromKey(const std::string &key) {
  return key == "lua" ? ProjectType::Lua : key == "cpp" ? ProjectType::Cpp : ProjectType::Mixed;
}

#ifdef _WIN32
const char *kExeSuffix = ".exe";
#else
const char *kExeSuffix = "";
#endif

struct Project {
  std::string name;
  fs::path path;
  ProjectType type = ProjectType::Lua;
  fs::path executable() const { return path / "bin" / (name + kExeSuffix); }
  bool compiled() const { return type != ProjectType::Lua; }
};

std::string quoted(const fs::path &path) { return "\"" + path.string() + "\""; }

// Starts a program without waiting for it
void launch(const std::string &command) {
#ifdef _WIN32
  std::system(("start \"\" " + command).c_str());
#else
  std::system((command + " > /dev/null 2>&1 &").c_str());
#endif
}

void openFolder(const fs::path &path) {
#ifdef _WIN32
  launch("explorer " + quoted(path));
#elif defined(__APPLE__)
  launch("open " + quoted(path));
#else
  launch("xdg-open " + quoted(path));
#endif
}

void openInCode(const fs::path &path) {
#ifdef __APPLE__
  // `code` is only on the PATH when VS Code installed its shell command; the app itself always opens
  if (std::system(("code " + quoted(path) + " > /dev/null 2>&1").c_str()) != 0)
    launch("open -a \"Visual Studio Code\" " + quoted(path));
#else
  launch("code " + quoted(path));
#endif
}

// What kind of project a folder is: project.json alone is Lua; CMake with scripts is both
ProjectType detectType(const fs::path &folder) {
  const bool cmake = fs::exists(folder / "CMakeLists.txt");
  if (!cmake)
    return ProjectType::Lua;
  return fs::exists(folder / "assets" / "scripts") ? ProjectType::Mixed : ProjectType::Cpp;
}

// --- Background tasks: one command at a time, its output into the log ----------------------------------------------

class Tasks {
public:
  ~Tasks() {
    if (m_thread.joinable())
      m_thread.join();
  }

  bool busy() const { return m_busy; }

  // Runs the commands in order (each through the shell), stopping at the first that fails; then `done` with whether
  // they all worked (on the main thread, in poll())
  void run(std::string title, std::vector<std::string> commands, std::function<void(bool)> done) {
    if (m_busy)
      return;
    if (m_thread.joinable())
      m_thread.join();
    m_busy = true;
    append("\n== " + title + "\n");
    m_thread = std::thread([this, commands = std::move(commands), done = std::move(done)] {
      bool ok = true;
      for (const std::string &command : commands) {
        append("$ " + command + "\n");
        FILE *pipe = popen((command + " 2>&1").c_str(), "r");
        if (!pipe) {
          append("could not start it\n");
          ok = false;
          break;
        }
        char line[1024];
        while (fgets(line, sizeof(line), pipe))
          append(line);
        if (pclose(pipe) != 0) {
          ok = false;
          break;
        }
      }
      append(ok ? "== done\n" : "== failed\n");
      std::lock_guard<std::mutex> lock(m_mutex);
      m_finished = [done, ok] { done(ok); };
    });
  }

  // On the main thread, every frame: hands over a finished task's continuation
  void poll() {
    std::function<void()> finished;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      finished.swap(m_finished);
    }
    if (finished) {
      m_busy = false;
      finished();
    }
  }

  void append(const std::string &text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_log += text;
    if (m_log.size() > 200000)
      m_log.erase(0, m_log.size() - 150000);
    m_logChanged = true;
  }

  std::string log(bool &changed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    changed = m_logChanged;
    m_logChanged = false;
    return m_log;
  }

private:
  std::thread m_thread;
  std::atomic<bool> m_busy{false};
  std::mutex m_mutex;
  std::string m_log;
  bool m_logChanged = false;
  std::function<void()> m_finished;
};

// --- Creating projects (the window and --create share it) ---------------------------------------------------------

struct ProjectFiles {
  fs::path engineDir;

  fs::path runner() const { return engineDir / "bin" / "runner" / (std::string("runner") + kExeSuffix); }

  std::string buildRunnerCommand() const {
    const fs::path build = engineDir / "build";
    return "cmake -S " + quoted(engineDir) + " -B " + quoted(build) + " && cmake --build " + quoted(build) +
           " --target runner -j";
  }

  // The template's files into the project (a C++ + Lua project is the C++ template with the mixed one over it),
  // named after the game, and for a Lua project the runner in bin/. Throws on failure.
  void create(const Project &project) const {
    copyTemplate(project.type == ProjectType::Lua ? "lua" : "cpp", project.path);
    if (project.type == ProjectType::Mixed)
      copyTemplate("mixed", project.path);
    fillPlaceholders(project.path, project.name);
    if (project.type == ProjectType::Lua)
      installRunner(project);
  }

  void copyTemplate(const std::string &name, const fs::path &into) const {
    const fs::path source = engineDir / "templates" / name;
    if (!fs::exists(source))
      throw std::runtime_error("no template at " + source.string());
    for (const auto &entry : fs::recursive_directory_iterator(source)) {
      const fs::path relative = fs::relative(entry.path(), source);
      const std::string first = relative.begin()->string();
      if (first == "build" || first == "bin")
        continue;
      const fs::path target = into / relative;
      if (entry.is_directory())
        fs::create_directories(target);
      else {
        fs::create_directories(target.parent_path());
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
      }
    }
  }

  void fillPlaceholders(const fs::path &folder, const std::string &name) const {
    const std::vector<std::string> textFiles = {".txt", ".cpp", ".h", ".md", ".json", ".lua"};
    for (const auto &entry : fs::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() ||
          std::find(textFiles.begin(), textFiles.end(), entry.path().extension().string()) == textFiles.end())
        continue;
      std::ifstream in(entry.path(), std::ios::binary);
      std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      in.close();
      const std::string original = text;
      replaceAll(text, "MyGame", name);
      replaceAll(text, "@ENGINE_DIR@", engineDir.generic_string());
      if (text != original)
        std::ofstream(entry.path(), std::ios::binary) << text;
    }
  }

  static void replaceAll(std::string &text, const std::string &from, const std::string &to) {
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
      text.replace(at, from.size(), to);
  }

  void installRunner(const Project &project) const {
    const fs::path bin = project.path / "bin";
    fs::create_directories(bin);
    for (const auto &entry : fs::recursive_directory_iterator(runner().parent_path())) {
      const fs::path relative = fs::relative(entry.path(), runner().parent_path());
      if (relative.begin()->string() == "cache")
        continue;
      const fs::path target = bin / relative;
      if (entry.is_directory())
        fs::create_directories(target);
      else
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
    }
    fs::rename(bin / runner().filename(), project.executable());
    fs::permissions(project.executable(), fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                              fs::perms::others_read | fs::perms::others_exec);
  }
};

// The project list the window shows, outside of it (--create adds to it too)
fs::path listFile() {
  const char *preferences = SDL_GetPrefPath("modular-engine", "launcher");
  return fs::path(preferences ? preferences : ".") / "projects.json";
}

bool validName(const std::string &name) { return std::regex_match(name, std::regex("[A-Za-z_][A-Za-z0-9_]*")); }

// --- The launcher ----------------------------------------------------------------------------------------------------

class Launcher {
public:
  Launcher(SDL_Window *window, fs::path engineDir)
      : m_window(window), m_engineDir(engineDir), m_files{std::move(engineDir)} {
    m_listFile = listFile();
    m_location = defaultLocation();
    load();
  }

  void frame() {
    m_tasks.poll();
    pickedFolder();

    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::TextUnformatted("Modular Engine");
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", m_engineDir.string().c_str());
    ImGui::Spacing();
    if (ImGui::Button("New project", ImVec2(140, 0)))
      m_newOpen = true;
    ImGui::SameLine();
    if (ImGui::Button("Add existing...", ImVec2(140, 0)))
      pickFolder(PickPurpose::AddExisting);
    if (m_tasks.busy()) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "  Working...");
    }
    if (!m_message.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(m_messageIsError ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.9f, 0.55f, 1.0f), "  %s",
                         m_message.c_str());
    }
    ImGui::Separator();

    projectTable();
    logPanel();
    ImGui::End();

    newProjectDialog();
  }

private:
  SDL_Window *m_window;
  fs::path m_engineDir;
  ProjectFiles m_files;
  fs::path m_listFile;
  std::vector<Project> m_projects;
  Tasks m_tasks;
  std::string m_message;
  bool m_messageIsError = false;

  // New project dialog
  bool m_newOpen = false;
  std::string m_newName = "MyGame";
  std::string m_location;
  int m_newType = 0;

  // The system's folder picker answers on its own time (and maybe thread)
  enum class PickPurpose { Location, AddExisting };
  PickPurpose m_pickPurpose = PickPurpose::Location;
  std::mutex m_pickMutex;
  std::string m_picked;
  bool m_hasPick = false;

  void say(std::string message, bool error = false) {
    m_message = std::move(message);
    m_messageIsError = error;
  }

  static std::string defaultLocation() {
    const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    return (fs::path(home ? home : ".") / "Games").string();
  }

  // --- The list ---------------------------------------------------------------------------------------------------

  void load() {
    std::ifstream file(m_listFile);
    if (!file)
      return;
    try {
      const nlohmann::json list = nlohmann::json::parse(file);
      for (const auto &entry : list.value("projects", nlohmann::json::array()))
        m_projects.push_back({entry.value("name", ""), fs::path(entry.value("path", "")),
                              typeFromKey(entry.value("type", "lua"))});
      m_location = list.value("location", m_location);
    } catch (const std::exception &) {
    }
  }

  void save() {
    nlohmann::json list = {{"location", m_location}, {"projects", nlohmann::json::array()}};
    for (const Project &project : m_projects)
    {
      nlohmann::json entry = nlohmann::json::object();
      entry["name"] = project.name;
      entry["path"] = project.path.string();
      entry["type"] = typeKey(project.type);
      list["projects"].push_back(entry);
    }
    std::ofstream(m_listFile) << list.dump(2);
  }

  void add(Project project) {
    m_projects.erase(std::remove_if(m_projects.begin(), m_projects.end(),
                                    [&](const Project &p) { return p.path == project.path; }),
                     m_projects.end());
    m_projects.insert(m_projects.begin(), std::move(project));
    save();
  }

  void projectTable() {
    const float logHeight = 220.0f;
    ImGui::BeginChild("Projects", ImVec2(0, -logHeight));
    if (m_projects.empty())
      ImGui::TextDisabled("No projects yet: New project creates one.");
    if (ImGui::BeginTable("projects", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                             ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 2.0f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 330.0f);
      ImGui::TableHeadersRow();
      int remove = -1;
      for (size_t i = 0; i < m_projects.size(); ++i) {
        Project &project = m_projects[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool exists = fs::exists(project.path);
        ImGui::TextUnformatted(project.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(typeName(project.type));
        ImGui::TableNextColumn();
        if (exists)
          ImGui::TextDisabled("%s", project.path.string().c_str());
        else
          ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "missing: %s", project.path.string().c_str());
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!exists || m_tasks.busy());
        if (ImGui::Button("Play"))
          play(project);
        if (project.compiled()) {
          ImGui::SameLine();
          if (ImGui::Button("Build"))
            build(project, false);
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!exists);
        ImGui::SameLine();
        if (ImGui::Button("VS Code"))
          openInCode(project.path);
        ImGui::SameLine();
        if (ImGui::Button("Folder"))
          openFolder(project.path);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
          remove = static_cast<int>(i);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Takes it off this list; the folder stays");
        ImGui::PopID();
      }
      ImGui::EndTable();
      if (remove >= 0) {
        m_projects.erase(m_projects.begin() + remove);
        save();
      }
    }
    ImGui::EndChild();
  }

  void logPanel() {
    ImGui::Separator();
    ImGui::TextDisabled("Output");
    ImGui::BeginChild("Log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    bool changed = false;
    const std::string log = m_tasks.log(changed);
    ImGui::TextUnformatted(log.c_str());
    if (changed)
      ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
  }

  // --- Folder picker ----------------------------------------------------------------------------------------------

  void pickFolder(PickPurpose purpose) {
    m_pickPurpose = purpose;
    SDL_ShowOpenFolderDialog(
        [](void *userdata, const char *const *files, int) {
          auto *self = static_cast<Launcher *>(userdata);
          if (!files || !files[0])
            return;
          std::lock_guard<std::mutex> lock(self->m_pickMutex);
          self->m_picked = files[0];
          self->m_hasPick = true;
        },
        this, m_window, m_location.c_str(), false);
  }

  void pickedFolder() {
    std::string picked;
    {
      std::lock_guard<std::mutex> lock(m_pickMutex);
      if (!m_hasPick)
        return;
      picked.swap(m_picked);
      m_hasPick = false;
    }
    if (m_pickPurpose == PickPurpose::Location) {
      m_location = picked;
      return;
    }
    const fs::path folder(picked);
    if (!fs::exists(folder / "project.json") && !fs::exists(folder / "CMakeLists.txt")) {
      say(folder.string() + " is not a game project (no project.json or CMakeLists.txt)", true);
      return;
    }
    // A Lua project is named in project.json; a C++ one after its folder
    std::string name = folder.filename().string();
    if (std::ifstream file{folder / "project.json"}) {
      try {
        name = nlohmann::json::parse(file).value("name", name);
      } catch (const std::exception &) {
      }
    }
    add({name, folder, detectType(folder)});
    say("Added " + name);
  }

  // --- New project ------------------------------------------------------------------------------------------------

  void newProjectDialog() {
    if (m_newOpen) {
      ImGui::OpenPopup("New project");
      m_newOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      return;
    ImGui::InputText("Name", &m_newName);
    ImGui::InputText("Location", &m_location);
    ImGui::SameLine();
    if (ImGui::Button("Choose..."))
      pickFolder(PickPurpose::Location);
    ImGui::TextDisabled("Created in %s", (fs::path(m_location) / m_newName).string().c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted("Type");
    ImGui::RadioButton("Lua", &m_newType, 0);
    ImGui::SameLine(160);
    ImGui::TextDisabled("scripts only: runs right away, nothing to compile");
    ImGui::RadioButton("C++", &m_newType, 1);
    ImGui::SameLine(160);
    ImGui::TextDisabled("game code in C++, built with CMake");
    ImGui::RadioButton("C++ + Lua", &m_newType, 2);
    ImGui::SameLine(160);
    ImGui::TextDisabled("C++ code and Lua scripts together");
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(120, 0))) {
      if (create())
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  bool create() {
    if (!validName(m_newName)) {
      say("The name is used for the program and C++ code: letters, digits and _ only", true);
      return false;
    }
    const fs::path folder = fs::path(m_location) / m_newName;
    std::error_code error;
    if (fs::exists(folder) && !fs::is_empty(folder, error)) {
      say(folder.string() + " already exists and is not empty", true);
      return false;
    }
    const ProjectType type = m_newType == 0 ? ProjectType::Lua : m_newType == 1 ? ProjectType::Cpp : ProjectType::Mixed;
    const Project project{m_newName, folder, type};

    if (type == ProjectType::Lua && !fs::exists(m_files.runner())) {
      // The prebuilt runner comes first: build it, then create
      m_tasks.run("Building the Lua runner (once)", {m_files.buildRunnerCommand()}, [this, project](bool ok) {
        if (ok)
          finishCreating(project);
        else
          say("Could not build the Lua runner; see the output", true);
      });
      return true;
    }
    finishCreating(project);
    return true;
  }

  void finishCreating(const Project &project) {
    try {
      m_files.create(project);
    } catch (const std::exception &exception) {
      say(std::string("Could not create it: ") + exception.what(), true);
      return;
    }
    add(project);
    save();
    if (project.compiled()) {
      say("Created " + project.name + "; building it");
      build(project, false);
    } else {
      say("Created " + project.name + ": Play runs it");
    }
  }

  // --- Build and play ---------------------------------------------------------------------------------------------

  void build(const Project &project, bool thenPlay) {
    const fs::path buildDir = project.path / "build";
    std::vector<std::string> commands;
    if (!fs::exists(buildDir / "CMakeCache.txt"))
      commands.push_back("cmake -S " + quoted(project.path) + " -B " + quoted(buildDir));
    commands.push_back("cmake --build " + quoted(buildDir) + " -j");
    m_tasks.run("Building " + project.name, commands, [this, project, thenPlay](bool ok) {
      if (!ok) {
        say("The build of " + project.name + " failed; the output shows why", true);
        return;
      }
      say("Built " + project.name);
      if (thenPlay)
        start(project);
    });
  }

  void play(const Project &project) {
    if (project.compiled())
      build(project, true); // only what changed is built again
    else
      start(project);
  }

  void start(const Project &project) {
    if (!fs::exists(project.executable())) {
      say(project.executable().string() + " is not there", true);
      return;
    }
    launch(quoted(project.executable()));
    say("Started " + project.name);
  }
};

// launcher --create <folder> <lua|cpp|mixed> [name]: the same as New project, from a terminal
int createFromCommandLine(const fs::path &engineDir, int argc, char **argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: launcher --create <folder> <lua|cpp|mixed> [name]\n");
    return 1;
  }
  const fs::path folder = fs::absolute(argv[2]);
  const std::string kind = argv[3];
  if (kind != "lua" && kind != "cpp" && kind != "mixed") {
    std::fprintf(stderr, "launcher: the type is lua, cpp or mixed, not '%s'\n", kind.c_str());
    return 1;
  }
  const std::string name = argc > 4 ? argv[4] : folder.filename().string();
  if (!validName(name)) {
    std::fprintf(stderr, "launcher: '%s' cannot name the program and C++ code: letters, digits and _ only\n",
                 name.c_str());
    return 1;
  }
  std::error_code error;
  if (fs::exists(folder) && !fs::is_empty(folder, error)) {
    std::fprintf(stderr, "launcher: %s is not empty\n", folder.string().c_str());
    return 1;
  }
  const Project project{name, folder, typeFromKey(kind)};
  const ProjectFiles files{engineDir};
  if (project.type == ProjectType::Lua && !fs::exists(files.runner())) {
    std::printf("Building the Lua runner (once)...\n");
    if (std::system(files.buildRunnerCommand().c_str()) != 0 || !fs::exists(files.runner())) {
      std::fprintf(stderr, "launcher: could not build the Lua runner\n");
      return 1;
    }
  }
  try {
    files.create(project);
  } catch (const std::exception &exception) {
    std::fprintf(stderr, "launcher: could not create it: %s\n", exception.what());
    return 1;
  }

  // On the window's list too
  nlohmann::json list = {{"projects", nlohmann::json::array()}};
  if (std::ifstream in{listFile()}) {
    try {
      list = nlohmann::json::parse(in);
    } catch (const std::exception &) {
    }
  }
  nlohmann::json entry = nlohmann::json::object();
  entry["name"] = name;
  entry["path"] = folder.string();
  entry["type"] = kind;
  if (!list.contains("projects") || !list["projects"].is_array())
    list["projects"] = nlohmann::json::array();
  list["projects"].insert(list["projects"].begin(), entry);
  std::ofstream(listFile()) << list.dump(2);

  std::printf("Created %s (%s) in %s\n\n  cd %s\n", name.c_str(), typeName(project.type), folder.string().c_str(),
              folder.string().c_str());
  if (project.compiled())
    std::printf("  cmake -B build && cmake --build build -j\n");
  std::printf("  ./bin/%s\n", name.c_str());
  return 0;
}

// The window's pixels as a BMP (the back buffer, before it is shown)
void saveScreenshot(SDL_Window *window, const std::string &path) {
  int width = 0, height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  std::vector<uint8_t> flipped(pixels.size());
  for (int y = 0; y < height; ++y)
    std::copy_n(&pixels[static_cast<size_t>(height - 1 - y) * width * 4], width * 4, &flipped[static_cast<size_t>(y) * width * 4]);
  SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ABGR8888, flipped.data(), width * 4);
  if (surface) {
    SDL_SaveBMP(surface, path.c_str());
    SDL_DestroySurface(surface);
  }
}

} // namespace

int main(int argc, char **argv) {
  // The engine checkout: bin/launcher/ is two folders below it
  const char *base = SDL_GetBasePath();
  const fs::path engineDir = fs::weakly_canonical(fs::path(base ? base : ".") / ".." / "..");
  if (argc > 1 && std::string(argv[1]) == "--create")
    return fs::exists(engineDir / "templates") ? createFromCommandLine(engineDir, argc, argv) : 1;
  std::string screenshot; // --screenshot <file.bmp>: saves the window after a few frames and quits
  if (argc > 2 && std::string(argv[1]) == "--screenshot")
    screenshot = fs::absolute(argv[2]).string();

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  if (!fs::exists(engineDir / "templates")) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Launcher",
                             "The launcher has to stay in the engine's bin/launcher folder (no templates found).",
                             nullptr);
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
  SDL_Window *window = SDL_CreateWindow("Modular Engine - Projects", 1100, 680,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
  if (!context || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
    std::fprintf(stderr, "Could not open the window: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().FrameRounding = 4.0f;
  ImGui::GetIO().FontGlobalScale = 1.25f; // ImGui's built-in font is small for a window of its own
  ImGui_ImplSDL3_InitForOpenGL(window, context);
  ImGui_ImplOpenGL3_Init("#version 410");

  {
    Launcher launcher(window, engineDir);
    bool running = true;
    int frames = 0;
    while (running) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
          running = false;
      }
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      launcher.frame();
      ImGui::Render();
      int width = 0, height = 0;
      SDL_GetWindowSizeInPixels(window, &width, &height);
      glViewport(0, 0, width, height);
      glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      if (!screenshot.empty() && ++frames == 10) {
        saveScreenshot(window, screenshot);
        running = false;
      }
      SDL_GL_SwapWindow(window);
    }
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
