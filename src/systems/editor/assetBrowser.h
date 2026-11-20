#pragma once
// The editor's asset browser: the project's assets/ folder as a folder view with breadcrumbs, filtered by kind and by
// a search box, from which assets are dragged into the scene or opened. It only lists and draws; what an asset
// does when dropped or opened is the editor's (EditorUISystem).
//
// The folder is listed again when asked (Refresh, reveal), when the window gets focus back, and every few seconds
// while the panel is open, spaced out by how long the last listing took, so a big project is not scanned often.
// Nothing is read while the panel is not drawn.

#include <string>
#include <vector>

class AssetBrowser {
public:
  enum class Kind { Folder, Model, Prefab, Scene, Sound, Script, Texture, UI, Other };

  // What the user did that the editor has to carry out
  struct Event {
    enum class Type { None, Open, SelectInstances } type = Type::None;
    std::string path; // relative to the assets folder, e.g. "prefabs/coin.json"
    Kind kind = Kind::Other;
  };

  // The drag and drop payload type; the data is the path, as above
  static constexpr const char *kPayload = "ASSET";

  // From the path relative to the assets folder: the folder a file is in (prefabs/ and scenes/ hold JSON) and its
  // extension decide
  static Kind classify(const std::string &relativePath);
  static const char *kindName(Kind kind);
  // The assets folder on disk, as the engine's asset path rule resolves it
  static std::string rootPath();
  // A file:// URL for a path (a native path is not a URL: C:\x\y on Windows)
  static std::string fileUrl(const std::string &path);
  // Shows the file's folder in the system's file manager
  static void revealInFileManager(const std::string &absolutePath);

  // Draws the contents of the panel inside the window the caller began. `appFocused`: the window has the keyboard
  // focus (a rising edge lists the folder again).
  Event draw(bool appFocused);
  // Goes to the file's folder and selects it, with no search or kind filter in the way
  void reveal(const std::string &relativePath);
  void refresh() { m_dirty = true; }

private:
  struct Entry {
    std::string path;   // relative to the assets folder, with '/'
    std::string name;   // the last part of it
    std::string folder; // the part before the name, "" at the top
    Kind kind = Kind::Other;
  };

  std::vector<Entry> m_entries; // everything, folders first inside each folder
  bool m_scanned = false;
  bool m_dirty = true;
  bool m_wasFocused = true;
  double m_lastScanTime = -1000.0;
  double m_scanSeconds = 0.0;
  unsigned m_generation = 0; // changes with each listing, to rebuild the shown rows

  std::string m_folder;                // being looked at, "" for the top
  int m_filter = -1;                   // a Kind shown alone; -1: all
  char m_search[128] = "";
  std::string m_selected;              // path of the highlighted row
  std::string m_scrollTo;              // path to bring into view next frame

  // The rows shown: rebuilt when the folder, the filter, the search or the listing changes
  std::vector<int> m_rows;
  std::string m_rowsFolder, m_rowsSearch;
  int m_rowsFilter = -2;
  unsigned m_rowsGeneration = 0;

  void scan();
  void rebuildRows();
};
