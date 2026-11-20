// The project launcher: creates game projects (Lua, C++ or both) from the engine's templates, builds and plays them,
// exports them to a folder (an .app bundle on macOS) to give to players, and opens them in VS Code or the file
// browser. The same as tools/new_game.py, with a window.
//
// It lives in the engine's bin/launcher; the engine (a checkout, or the prebuilt package of tools/package_engine.py,
// which has the templates and the Lua runner but no sources) is two folders up. The project list is kept in the
// user's preferences folder.

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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <regex>
#include <sstream>
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

// --- The build tools -------------------------------------------------------------------------------------------------

// CMake and a compiler, which only C++ projects need. A program started from the macOS Finder does not get the shell's
// PATH (just /usr/bin:/bin:/usr/sbin:/sbin), so cmake from Homebrew or CMake.app would not be found by name: it is
// looked for in the usual places and run by its full path.
struct Toolchain {
  fs::path cmake; // empty when not found

  static Toolchain locate() {
    std::vector<fs::path> folders;
    if (const char *path = std::getenv("PATH")) {
#ifdef _WIN32
      const char separator = ';';
#else
      const char separator = ':';
#endif
      std::stringstream stream(path);
      std::string folder;
      while (std::getline(stream, folder, separator))
        if (!folder.empty())
          folders.push_back(folder);
    }
    for (const fs::path &folder : commonFolders())
      folders.push_back(folder);
    for (const fs::path &folder : folders) {
      std::error_code error;
      const fs::path candidate = folder / (std::string("cmake") + kExeSuffix);
      if (fs::is_regular_file(candidate, error))
        return {candidate};
    }
    return {};
  }

  static std::vector<fs::path> commonFolders() {
#if defined(__APPLE__)
    return {"/opt/homebrew/bin", "/usr/local/bin", "/Applications/CMake.app/Contents/bin"};
#elif defined(_WIN32)
    return {"C:\\Program Files\\CMake\\bin", "C:\\Program Files (x86)\\CMake\\bin"};
#else
    return {"/usr/local/bin", "/usr/bin", "/snap/bin"};
#endif
  }

  // A cmake command line. CMake runs git, ninja and the compiler by name, so the child's PATH starts with cmake's own
  // folder and, on macOS, Homebrew's.
  std::string command(const std::string &arguments) const {
    std::string folders = cmake.parent_path().string();
#ifdef __APPLE__
    for (const char *homebrew : {"/opt/homebrew/bin", "/usr/local/bin"})
      if (folders != homebrew)
        folders += std::string(":") + homebrew;
#endif
#ifdef _WIN32
    return "set \"PATH=" + folders + ";%PATH%\" && " + quoted(cmake) + " " + arguments;
#else
    return "PATH=\"" + folders + ":$PATH\" " + quoted(cmake) + " " + arguments;
#endif
  }

  // Empty when C++ projects can be built; otherwise what is missing and what to install
  std::string problem() const {
    std::string text;
#ifdef __APPLE__
    // Without the command line tools there is no compiler, and no git for downloading the dependencies
    if (std::system("xcode-select -p > /dev/null 2>&1") != 0)
      text += "No compiler found. In a terminal, run: xcode-select --install (it installs the compiler and git).\n";
    if (cmake.empty())
      text += "CMake not found. Install it with Homebrew (brew install cmake) or from cmake.org, then try again.\n";
#elif defined(_WIN32)
    if (cmake.empty())
      text += "CMake not found. Install CMake (cmake.org) and Visual Studio Build Tools with the C++ workload, then "
              "try again.\n";
#else
    if (cmake.empty())
      text += "CMake not found. Install a C++ compiler, git and cmake with your package manager (for example: "
              "sudo apt install build-essential git cmake), then try again.\n";
#endif
    return text;
  }
};

// A project's name: a Lua project's is in project.json, a C++ one's is the program CMake builds (the folder's name
// when the project was created)
std::string readProjectName(const fs::path &folder) {
  const std::string name = folder.filename().string();
  if (std::ifstream file{folder / "project.json"}) {
    try {
      return nlohmann::json::parse(file).value("name", name);
    } catch (const std::exception &) {
      return name;
    }
  }
  if (std::ifstream file{folder / "CMakeLists.txt"}) {
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::smatch match;
    if (std::regex_search(text, match, std::regex("modular_engine_add_game\\(\\s*([A-Za-z_][A-Za-z0-9_]*)")))
      return match[1];
  }
  return name;
}

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

  // The packaged engine has no sources: its runner is already built, and cannot be
  bool hasSources() const { return fs::exists(engineDir / "CMakeLists.txt"); }

  std::vector<std::string> buildRunnerCommands(const Toolchain &toolchain) const {
    const fs::path build = engineDir / "build";
    return {toolchain.command("-S " + quoted(engineDir) + " -B " + quoted(build)),
            toolchain.command("--build " + quoted(build) + " --target runner -j")};
  }

  // The template's files into the project (a C++ + Lua project is the C++ template with the mixed one over it),
  // named after the game, and for a Lua project the runner in bin/. Projects with Lua also get the editor's Lua
  // settings and API definitions (templates/common/lua). Throws on failure.
  //
  // An empty project is the same with the example game left out: from the example template only what
  // templates/empty/keep.txt lists is taken (the input bindings, the pause menu, CMake's file), and then
  // templates/empty/<lua|cpp|mixed> (the minimal scene, the stubbed game code, the README) goes over it.
  void create(const Project &project, bool empty = false) const {
    const std::string base = project.type == ProjectType::Lua ? "lua" : "cpp";
    if (empty) {
      copyTemplate(base, project.path, readKeepList());
      copyTemplate("empty/" + base, project.path);
      if (project.type == ProjectType::Mixed)
        copyTemplate("empty/mixed", project.path);
    } else {
      copyTemplate(base, project.path);
      if (project.type == ProjectType::Mixed)
        copyTemplate("mixed", project.path);
    }
    if (project.type != ProjectType::Cpp)
      copyTemplate("common/lua", project.path);
    fillPlaceholders(project.path, project.name);
    if (project.type == ProjectType::Lua)
      installRunner(project);
  }

  // The paths templates/empty/keep.txt lists (one per line; # starts a comment)
  std::vector<std::string> readKeepList() const {
    const fs::path path = engineDir / "templates" / "empty" / "keep.txt";
    std::ifstream file(path);
    if (!file)
      throw std::runtime_error("no empty-project list at " + path.string());
    std::vector<std::string> keep;
    for (std::string line; std::getline(file, line);) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
      if (!line.empty() && line[0] != '#')
        keep.push_back(line);
    }
    return keep;
  }

  // The files of templates/<name> into `into`; with `only`, just those paths (a folder's name takes what is in it)
  void copyTemplate(const std::string &name, const fs::path &into, const std::vector<std::string> &only = {}) const {
    const fs::path source = engineDir / "templates" / name;
    if (!fs::exists(source))
      throw std::runtime_error("no template at " + source.string());
    const auto wanted = [&only](const fs::path &relative) {
      const std::string path = relative.generic_string();
      for (const std::string &entry : only)
        if (path == entry || path.rfind(entry + "/", 0) == 0)
          return true;
      return false;
    };
    for (const auto &entry : fs::recursive_directory_iterator(source)) {
      const fs::path relative = fs::relative(entry.path(), source);
      const std::string first = relative.begin()->string();
      if (first == "build" || first == "bin" || entry.path().filename() == ".DS_Store")
        continue;
      const fs::path target = into / relative;
      if (entry.is_directory()) {
        if (only.empty())
          fs::create_directories(target);
      } else if (only.empty() || wanted(relative)) {
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
      // With no engine sources here (the package), an empty directory makes the project download the engine
      replaceAll(text, "@ENGINE_DIR@", hasSources() ? engineDir.generic_string() : "");
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
      if (relative.begin()->string() == "cache" || entry.path().filename() == ".DS_Store")
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

// --- Exporting: the files a player needs, and nothing else -----------------------------------------------------------

struct Exporter {
  static fs::path releaseDir(const Project &project) { return project.path / "build-release"; }

  // A C++ project is built in Release in its own build folder and its own output folder, so the build/ and bin/ the
  // developer plays from stay as they are; a Lua project has nothing to build
  static std::vector<std::string> buildCommands(const Project &project, const Toolchain &toolchain) {
    if (!project.compiled())
      return {};
    const fs::path build = releaseDir(project);
    return {toolchain.command("-S " + quoted(project.path) + " -B " + quoted(build) +
                              " -DCMAKE_BUILD_TYPE=Release -DMODULAR_ENGINE_GAME_OUTPUT_DIR=" + quoted(build / "bin")),
            toolchain.command("--build " + quoted(build) + " --config Release -j")};
  }

  // The game from the built program, SDL, the engine's assets and the project's assets into outputDir; returns what
  // was made: <name>.app on macOS, a <name> folder elsewhere. Throws on failure.
  static fs::path assemble(const Project &project, const fs::path &outputDir) {
    const fs::path built = project.compiled() ? releaseDir(project) / "bin" : project.path / "bin";
    if (!fs::exists(built / (project.name + kExeSuffix)))
      throw std::runtime_error((built / (project.name + kExeSuffix)).string() + " is not there (build it first)");
    if (!fs::exists(project.path / "assets"))
      throw std::runtime_error(project.path.string() + " has no assets folder");
    if (!project.compiled() && !fs::exists(project.path / "project.json"))
      throw std::runtime_error(project.path.string() + " has no project.json");

    // The program looks for ../assets and ../project.json from its base folder, which is its own folder, except in a
    // macOS app bundle, where SDL makes it Contents/Resources: there engine-assets goes with it, and the program and
    // SDL's library stay in Contents/MacOS
#ifdef __APPLE__
    const fs::path result = outputDir / (project.name + ".app");
    const fs::path root = result / "Contents";
    const fs::path program = root / "MacOS";
    const fs::path baseFolder = root / "Resources";
#else
    const fs::path result = outputDir / project.name;
    const fs::path root = result;
    const fs::path program = root / "bin";
    const fs::path baseFolder = program;
#endif
    std::error_code error;
    fs::remove_all(result, error);
    fs::create_directories(program);

    copyTree(built, program, [](const fs::path &relative) {
      // What a run leaves behind: the texture and mesh caches, saved settings, screenshots, logs
      const std::string extension = relative.extension().string();
      return relative.begin()->string() == "cache" || extension == ".ini" || extension == ".bmp" ||
             extension == ".log" || extension == ".pdb" || extension == ".ilk";
    });
    if (baseFolder != program) {
      fs::create_directories(baseFolder);
      fs::rename(program / "engine-assets", baseFolder / "engine-assets");
    }
    copyTree(project.path / "assets", root / "assets", [](const fs::path &) { return false; });
    if (!project.compiled())
      fs::copy_file(project.path / "project.json", root / "project.json");
#ifdef __APPLE__
    writeInfoPlist(project, root / "Info.plist");
    // The programs are signed ad hoc by the linker; signing the bundle too lets macOS treat it as one app. Optional.
    std::system(("codesign --force --deep --sign - " + quoted(result) + " > /dev/null 2>&1").c_str());
#endif
    return result;
  }

  static void copyTree(const fs::path &from, const fs::path &to, const std::function<bool(const fs::path &)> &skip) {
    fs::create_directories(to);
    for (auto it = fs::recursive_directory_iterator(from); it != fs::recursive_directory_iterator(); ++it) {
      const fs::path relative = fs::relative(it->path(), from);
      if (it->path().filename() == ".DS_Store" || skip(relative)) {
        if (it->is_directory() && !it->is_symlink())
          it.disable_recursion_pending();
        continue;
      }
      const fs::path target = to / relative;
      if (it->is_symlink())
        fs::copy_symlink(it->path(), target);
      else if (it->is_directory())
        fs::create_directories(target);
      else
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing);
    }
  }

  static void writeInfoPlist(const Project &project, const fs::path &path) {
    std::ofstream(path) << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                           "<plist version=\"1.0\">\n<dict>\n"
                           "  <key>CFBundleExecutable</key><string>" << project.name << "</string>\n"
                           "  <key>CFBundleIdentifier</key><string>com.modular-engine." << project.name << "</string>\n"
                           "  <key>CFBundleName</key><string>" << project.name << "</string>\n"
                           "  <key>CFBundlePackageType</key><string>APPL</string>\n"
                           "  <key>NSHighResolutionCapable</key><true/>\n"
                           "</dict>\n</plist>\n";
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
  int m_newContent = 0; // 0: the example game, 1: an empty project

  // The system's folder picker answers on its own time (and maybe thread)
  enum class PickPurpose { Location, AddExisting, ExportTo };
  PickPurpose m_pickPurpose = PickPurpose::Location;
  std::mutex m_pickMutex;
  std::string m_picked;
  bool m_hasPick = false;
  Project m_exporting; // the project whose Export asked for a folder

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
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 440.0f);
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
        ImGui::SameLine();
        if (ImGui::Button("Export")) {
          m_exporting = project;
          pickFolder(PickPurpose::ExportTo);
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Makes the game to give to players (an app on macOS), in a folder you choose");
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
    if (m_pickPurpose == PickPurpose::ExportTo) {
      exportProject(m_exporting, picked);
      return;
    }
    const fs::path folder(picked);
    if (!fs::exists(folder / "project.json") && !fs::exists(folder / "CMakeLists.txt")) {
      say(folder.string() + " is not a game project (no project.json or CMakeLists.txt)", true);
      return;
    }
    const std::string name = readProjectName(folder);
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
    ImGui::TextUnformatted("Start from");
    ImGui::RadioButton("Example game", &m_newContent, 0);
    ImGui::SameLine(160);
    ImGui::TextDisabled("a small playable game to read and change");
    ImGui::RadioButton("Empty project", &m_newContent, 1);
    ImGui::SameLine(160);
    ImGui::TextDisabled("a camera, a light, a sky and a floor, with the code stubbed");
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
    const bool empty = m_newContent == 1;

    if (type == ProjectType::Lua && !fs::exists(m_files.runner())) {
      if (!m_files.hasSources()) {
        say("This engine has no Lua runner (bin/runner) and no sources to build one from", true);
        return false;
      }
      const Toolchain toolchain = Toolchain::locate();
      if (!requireToolchain(toolchain))
        return false;
      // The prebuilt runner comes first: build it, then create
      m_tasks.run("Building the Lua runner (once)", m_files.buildRunnerCommands(toolchain), [this, project, empty](bool ok) {
        if (ok)
          finishCreating(project, empty);
        else
          say("Could not build the Lua runner; see the output", true);
      });
      return true;
    }
    finishCreating(project, empty);
    return true;
  }

  void finishCreating(const Project &project, bool empty) {
    try {
      m_files.create(project, empty);
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

  // Whether C++ can be built here; if not, the output says what to install
  bool requireToolchain(const Toolchain &toolchain) {
    const std::string problem = toolchain.problem();
    if (problem.empty())
      return true;
    m_tasks.append("\n== C++ needs CMake and a compiler\n" + problem);
    say("C++ needs CMake and a compiler: the output says what to install", true);
    return false;
  }

  void build(const Project &project, bool thenPlay) {
    const Toolchain toolchain = Toolchain::locate();
    if (!requireToolchain(toolchain))
      return;
    const fs::path buildDir = project.path / "build";
    std::vector<std::string> commands;
    if (!fs::exists(buildDir / "CMakeCache.txt"))
      commands.push_back(toolchain.command("-S " + quoted(project.path) + " -B " + quoted(buildDir)));
    commands.push_back(toolchain.command("--build " + quoted(buildDir) + " -j"));
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

  // A C++ project is built in Release first; then the files a player needs go into the folder
  void exportProject(const Project &project, const fs::path &folder) {
    const Toolchain toolchain = Toolchain::locate();
    if (project.compiled() && !requireToolchain(toolchain))
      return;
    m_tasks.run("Exporting " + project.name, Exporter::buildCommands(project, toolchain),
                [this, project, folder](bool ok) {
                  if (!ok) {
                    say("The build of " + project.name + " failed; the output shows why", true);
                    return;
                  }
                  try {
                    const fs::path result = Exporter::assemble(project, folder);
                    m_tasks.append("Exported to " + result.string() + "\n");
                    say("Exported " + project.name + " to " + result.string());
                    openFolder(folder);
                  } catch (const std::exception &exception) {
                    say(std::string("Could not export: ") + exception.what(), true);
                  }
                });
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

// launcher --create <folder> <lua|cpp|mixed> [name] [--empty]: the same as New project, from a terminal. The project
// is the example game, or with --empty (anywhere after the folder) an empty one.
int createFromCommandLine(const fs::path &engineDir, int argc, char **argv) {
  std::vector<std::string> arguments;
  bool empty = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--empty")
      empty = true;
    else
      arguments.push_back(argv[i]);
  }
  if (arguments.size() < 2 || arguments.size() > 3) {
    std::fprintf(stderr,
                 "usage: launcher --create <folder> <lua|cpp|mixed> [name] [--empty]\n"
                 "  --empty: a camera, a light, a sky and a floor with the code stubbed, instead of the example game\n");
    return 1;
  }
  const fs::path folder = fs::absolute(arguments[0]);
  const std::string kind = arguments[1];
  if (kind != "lua" && kind != "cpp" && kind != "mixed") {
    std::fprintf(stderr, "launcher: the type is lua, cpp or mixed, not '%s'\n", kind.c_str());
    return 1;
  }
  const std::string name = arguments.size() > 2 ? arguments[2] : folder.filename().string();
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
    if (!files.hasSources()) {
      std::fprintf(stderr, "launcher: this engine has no Lua runner (bin/runner) and no sources to build one from\n");
      return 1;
    }
    const Toolchain toolchain = Toolchain::locate();
    if (const std::string problem = toolchain.problem(); !problem.empty()) {
      std::fprintf(stderr, "launcher: building the Lua runner needs CMake and a compiler\n%s", problem.c_str());
      return 1;
    }
    std::printf("Building the Lua runner (once)...\n");
    for (const std::string &command : files.buildRunnerCommands(toolchain))
      if (std::system(command.c_str()) != 0) {
        std::fprintf(stderr, "launcher: could not build the Lua runner\n");
        return 1;
      }
    if (!fs::exists(files.runner())) {
      std::fprintf(stderr, "launcher: could not build the Lua runner\n");
      return 1;
    }
  }
  try {
    files.create(project, empty);
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

  std::printf("Created %s (%s%s) in %s\n\n  cd %s\n", name.c_str(), typeName(project.type), empty ? ", empty" : "",
              folder.string().c_str(), folder.string().c_str());
  if (project.compiled())
    std::printf("  cmake -B build && cmake --build build -j\n");
  std::printf("  ./bin/%s\n", name.c_str());
  return 0;
}

// launcher --export <projectDir> <outputDir>: the same as the window's Export
int exportFromCommandLine(int argc, char **argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: launcher --export <projectDir> <outputDir>\n");
    return 1;
  }
  const fs::path folder = fs::absolute(argv[2]).lexically_normal();
  const fs::path output = fs::absolute(argv[3]).lexically_normal();
  if (!fs::exists(folder / "project.json") && !fs::exists(folder / "CMakeLists.txt")) {
    std::fprintf(stderr, "launcher: %s is not a game project (no project.json or CMakeLists.txt)\n",
                 folder.string().c_str());
    return 1;
  }
  const Project project{readProjectName(folder), folder, detectType(folder)};
  const Toolchain toolchain = Toolchain::locate();
  if (project.compiled()) {
    if (const std::string problem = toolchain.problem(); !problem.empty()) {
      std::fprintf(stderr, "launcher: C++ needs CMake and a compiler\n%s", problem.c_str());
      return 1;
    }
  }
  for (const std::string &command : Exporter::buildCommands(project, toolchain)) {
    std::printf("$ %s\n", command.c_str());
    std::fflush(stdout);
    if (std::system(command.c_str()) != 0) {
      std::fprintf(stderr, "launcher: the build of %s failed\n", project.name.c_str());
      return 1;
    }
  }
  try {
    std::printf("Exported %s to %s\n", project.name.c_str(), Exporter::assemble(project, output).string().c_str());
  } catch (const std::exception &exception) {
    std::fprintf(stderr, "launcher: could not export: %s\n", exception.what());
    return 1;
  }
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
  if (argc > 1 && std::string(argv[1]) == "--export")
    return fs::exists(engineDir / "templates") ? exportFromCommandLine(argc, argv) : 1;
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
