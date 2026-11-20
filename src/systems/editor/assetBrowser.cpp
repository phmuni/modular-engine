// The asset browser panel (see assetBrowser.h).

#include "systems/editor/assetBrowser.h"

#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_misc.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <imgui/imgui.h>

namespace fs = std::filesystem;

namespace {
constexpr size_t kMaxEntries = 50000; // a folder with more than this is not an asset folder

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

ImVec4 colorOf(AssetBrowser::Kind kind) {
  switch (kind) {
  case AssetBrowser::Kind::Folder: return ImVec4(0.95f, 0.8f, 0.4f, 1.0f);
  case AssetBrowser::Kind::Model: return ImVec4(0.55f, 0.8f, 1.0f, 1.0f);
  case AssetBrowser::Kind::Prefab: return ImVec4(0.45f, 0.65f, 1.0f, 1.0f);
  case AssetBrowser::Kind::Scene: return ImVec4(0.7f, 0.9f, 0.5f, 1.0f);
  case AssetBrowser::Kind::Sound: return ImVec4(0.95f, 0.6f, 0.9f, 1.0f);
  case AssetBrowser::Kind::Script: return ImVec4(0.6f, 0.95f, 0.8f, 1.0f);
  case AssetBrowser::Kind::Shader: return ImVec4(0.8f, 0.7f, 1.0f, 1.0f);
  case AssetBrowser::Kind::Texture: return ImVec4(1.0f, 0.7f, 0.5f, 1.0f);
  case AssetBrowser::Kind::UI: return ImVec4(0.85f, 0.85f, 0.6f, 1.0f);
  case AssetBrowser::Kind::Animator: return ImVec4(0.95f, 0.75f, 0.55f, 1.0f);
  case AssetBrowser::Kind::Other: break;
  }
  return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
}
} // namespace

AssetBrowser::Kind AssetBrowser::classify(const std::string &relativePath) {
  const std::string extension = lower(fs::path(relativePath).extension().generic_string());
  if (extension == ".obj" || extension == ".glb" || extension == ".gltf")
    return Kind::Model;
  if (extension == ".json") {
    const std::string top = relativePath.substr(0, relativePath.find('/'));
    if (top == "prefabs")
      return Kind::Prefab;
    if (top == "scenes")
      return Kind::Scene;
    if (top == "animators")
      return Kind::Animator;
    return Kind::Other;
  }
  if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac")
    return Kind::Sound;
  if (extension == ".lua")
    return Kind::Script;
  if (extension == ".glsl")
    return Kind::Shader;
  if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp" ||
      extension == ".hdr")
    return Kind::Texture;
  if (extension == ".rml" || extension == ".rcss" || extension == ".ttf" || extension == ".otf")
    return Kind::UI;
  return Kind::Other;
}

const char *AssetBrowser::kindName(Kind kind) {
  switch (kind) {
  case Kind::Folder: return "Folder";
  case Kind::Model: return "Model";
  case Kind::Prefab: return "Prefab";
  case Kind::Scene: return "Scene";
  case Kind::Sound: return "Sound";
  case Kind::Script: return "Script";
  case Kind::Shader: return "Shader";
  case Kind::Texture: return "Texture";
  case Kind::UI: return "UI";
  case Kind::Animator: return "Animator";
  case Kind::Other: break;
  }
  return "Other";
}

std::string AssetBrowser::rootPath() {
  fs::path root = fs::path(EngineConfig::resolvePath(EngineConfig::ASSETS_PREFIX)).lexically_normal();
  if (!root.has_filename())
    root = root.parent_path();
  return root.generic_string();
}

std::string AssetBrowser::fileUrl(const std::string &path) {
  std::error_code error;
  fs::path absolute = fs::absolute(path, error);
  if (error)
    absolute = path;
  const std::string generic = absolute.generic_string();
  std::string url = generic.rfind('/', 0) == 0 ? "file://" : "file:///";
  for (unsigned char c : generic) {
    if (std::isalnum(c) || std::strchr("/-._~:", c)) {
      url += static_cast<char>(c);
    } else {
      static const char *hex = "0123456789ABCDEF";
      url += '%';
      url += hex[c >> 4];
      url += hex[c & 15];
    }
  }
  return url;
}

void AssetBrowser::revealInFileManager(const std::string &absolutePath) {
  std::error_code error;
  fs::path folder = fs::path(absolutePath);
  if (!fs::is_directory(folder, error))
    folder = folder.parent_path();
  if (!SDL_OpenURL(fileUrl(folder.generic_string()).c_str()))
    LOG_W("[Assets] Could not show %s: %s", folder.generic_string().c_str(), SDL_GetError());
}

void AssetBrowser::scan() {
  const auto start = std::chrono::steady_clock::now();
  m_entries.clear();
  const fs::path root = rootPath();
  std::error_code error;
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
       !error && it != end; it.increment(error)) {
    const std::string name = it->path().filename().generic_string();
    std::error_code entryError;
    const bool directory = it->is_directory(entryError);
    if (name.empty() || name[0] == '.') { // .DS_Store, .git...
      if (directory)
        it.disable_recursion_pending();
      continue;
    }
    Entry entry;
    entry.path = it->path().lexically_relative(root).generic_string();
    entry.name = name;
    const size_t slash = entry.path.rfind('/');
    entry.folder = slash == std::string::npos ? std::string() : entry.path.substr(0, slash);
    entry.kind = directory ? Kind::Folder : classify(entry.path);
    m_entries.push_back(std::move(entry));
    if (m_entries.size() >= kMaxEntries)
      break;
  }
  m_scanned = true;
  m_dirty = false;
  ++m_generation;
  m_scanSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  m_lastScanTime = ImGui::GetTime();
}

void AssetBrowser::rebuildRows() {
  m_rows.clear();
  const std::string needle = lower(m_search);
  const bool searching = !needle.empty();
  for (size_t i = 0; i < m_entries.size(); ++i) {
    const Entry &entry = m_entries[i];
    const bool kindShown = m_filter < 0 || static_cast<int>(entry.kind) == m_filter;
    if (searching) {
      if (entry.kind != Kind::Folder && kindShown && lower(entry.path).find(needle) != std::string::npos)
        m_rows.push_back(static_cast<int>(i));
    } else if (entry.folder == m_folder && (entry.kind == Kind::Folder || kindShown)) {
      m_rows.push_back(static_cast<int>(i)); // folders stay under a filter, to get around
    }
  }
  std::sort(m_rows.begin(), m_rows.end(), [&](int a, int b) {
    const Entry &x = m_entries[a], &y = m_entries[b];
    if ((x.kind == Kind::Folder) != (y.kind == Kind::Folder))
      return x.kind == Kind::Folder;
    return lower(searching ? x.path : x.name) < lower(searching ? y.path : y.name);
  });
  m_rowsFolder = m_folder;
  m_rowsSearch = m_search;
  m_rowsFilter = m_filter;
  m_rowsGeneration = m_generation;
}

void AssetBrowser::reveal(const std::string &relativePath) {
  const size_t slash = relativePath.rfind('/');
  m_folder = slash == std::string::npos ? std::string() : relativePath.substr(0, slash);
  m_filter = -1;
  m_search[0] = '\0';
  m_selected = relativePath;
  m_scrollTo = relativePath;
}

AssetBrowser::Event AssetBrowser::draw(bool appFocused) {
  Event event;
  // Listed again: asked for, focus came back (files may have changed meanwhile), or a while has passed (longer for
  // a listing that took long)
  if (appFocused && !m_wasFocused)
    m_dirty = true;
  m_wasFocused = appFocused;
  if (!m_scanned || m_dirty || ImGui::GetTime() - m_lastScanTime > std::max(5.0, m_scanSeconds * 50.0))
    scan();

  if (ImGui::SmallButton("Refresh"))
    m_dirty = true;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("List the assets folder again (it is also listed when the window gets focus back)");
  struct FilterChoice {
    const char *label;
    int kind;
  };
  static const FilterChoice filters[] = {{"All", -1},
                                         {"Models", static_cast<int>(Kind::Model)},
                                         {"Prefabs", static_cast<int>(Kind::Prefab)},
                                         {"Scenes", static_cast<int>(Kind::Scene)},
                                         {"Sounds", static_cast<int>(Kind::Sound)},
                                         {"Scripts", static_cast<int>(Kind::Script)},
                                         {"Shaders", static_cast<int>(Kind::Shader)},
                                         {"Textures", static_cast<int>(Kind::Texture)},
                                         {"UI", static_cast<int>(Kind::UI)},
                                         {"Animators", static_cast<int>(Kind::Animator)}};
  for (const FilterChoice &filter : filters) {
    ImGui::SameLine();
    const bool active = m_filter == filter.kind;
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton(filter.label))
      m_filter = filter.kind;
    if (active)
      ImGui::PopStyleColor();
  }

  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  ImGui::InputTextWithHint("##assetSearch", "Search", m_search, sizeof(m_search));
  ImGui::SameLine();
  const bool searching = m_search[0] != '\0';
  if (searching) {
    ImGui::TextDisabled("%zu result(s) in all folders", m_rows.size());
  } else {
    // Breadcrumbs: each part goes back to that folder
    if (ImGui::SmallButton("assets"))
      m_folder.clear();
    std::string walked;
    size_t begin = 0;
    int level = 0;
    while (begin < m_folder.size()) {
      size_t end = m_folder.find('/', begin);
      if (end == std::string::npos)
        end = m_folder.size();
      walked = m_folder.substr(0, end);
      ImGui::SameLine(0.0f, 2.0f);
      ImGui::TextUnformatted("/");
      ImGui::SameLine(0.0f, 2.0f);
      ImGui::PushID(level++);
      if (ImGui::SmallButton(m_folder.substr(begin, end - begin).c_str()))
        m_folder = walked;
      ImGui::PopID();
      begin = end + 1;
    }
  }

  if (m_rowsGeneration != m_generation || m_rowsFolder != m_folder || m_rowsSearch != m_search ||
      m_rowsFilter != m_filter)
    rebuildRows();

  constexpr ImGuiTableFlags kTable = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                     ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV;
  if (!ImGui::BeginTable("##assetRows", 3, kTable, ImVec2(0.0f, 0.0f)))
    return event;
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
  ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableHeadersRow();

  // A row up, unless at the top
  if (!searching && !m_folder.empty()) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (ImGui::Selectable("..##up", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      const size_t slash = m_folder.rfind('/');
      m_folder = slash == std::string::npos ? std::string() : m_folder.substr(0, slash);
    }
  }

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(m_rows.size()));
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const Entry &entry = m_entries[m_rows[row]];
      ImGui::PushID(m_rows[row]);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushStyleColor(ImGuiCol_Text, colorOf(entry.kind));
      const bool selected = entry.path == m_selected;
      if (ImGui::Selectable(entry.name.c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
        m_selected = entry.path;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          if (entry.kind == Kind::Folder) {
            m_folder = entry.path;
            m_search[0] = '\0';
          } else {
            event = {Event::Type::Open, entry.path, entry.kind};
          }
        }
      }
      ImGui::PopStyleColor();
      if (m_scrollTo == entry.path) {
        ImGui::SetScrollHereY();
        m_scrollTo.clear();
      }

      if (entry.kind != Kind::Folder && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kPayload, entry.path.c_str(), entry.path.size() + 1);
        ImGui::Text("%s  (%s)", entry.name.c_str(), kindName(entry.kind));
        switch (entry.kind) {
        case Kind::Model:
        case Kind::Prefab: ImGui::TextDisabled("Drop in the view to place it"); break;
        case Kind::Script: ImGui::TextDisabled("Drop on an entity to give it this script"); break;
        case Kind::Sound: ImGui::TextDisabled("Drop on an entity to give it this sound"); break;
        case Kind::Shader: ImGui::TextDisabled("Drop on a part's Shader field (Material section)"); break;
        case Kind::Scene: ImGui::TextDisabled("Drop in the view to load it"); break;
        default: ImGui::TextDisabled("Nothing to do with it in the scene"); break;
        }
        ImGui::EndDragDropSource();
      }

      if (ImGui::BeginPopupContextItem("##assetMenu")) {
        m_selected = entry.path;
        const char *openLabel = entry.kind == Kind::Folder   ? "Open"
                                : entry.kind == Kind::Scene  ? "Load scene"
                                : entry.kind == Kind::Script ? "Open in editor"
                                : (entry.kind == Kind::Model || entry.kind == Kind::Prefab)
                                    ? "Place in front of the camera"
                                    : "Open";
        if (ImGui::MenuItem(openLabel)) {
          if (entry.kind == Kind::Folder) {
            m_folder = entry.path;
            m_search[0] = '\0';
          } else {
            event = {Event::Type::Open, entry.path, entry.kind};
          }
        }
        if (entry.kind == Kind::Prefab && ImGui::MenuItem("Select instances"))
          event = {Event::Type::SelectInstances, entry.path, entry.kind};
        ImGui::Separator();
        if (ImGui::MenuItem("Reveal in file manager"))
          revealInFileManager(rootPath() + "/" + entry.path);
        if (ImGui::MenuItem("Copy relative path"))
          SDL_SetClipboardText(entry.path.c_str());
        ImGui::EndPopup();
      }

      ImGui::TableNextColumn();
      ImGui::TextColored(colorOf(entry.kind), "%s", kindName(entry.kind));
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", entry.folder.c_str());
      ImGui::PopID();
    }
  }
  if (m_rows.empty()) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (m_entries.empty())
      ImGui::TextDisabled("No assets in %s", rootPath().c_str());
    else
      ImGui::TextDisabled("Nothing here");
  }
  ImGui::EndTable();
  return event;
}
