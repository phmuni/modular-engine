#pragma once
// Helpers for caches that store processed assets in bin/cache (see cacheRoot): entry names, source-file stamps and binary I/O.

#include "foundation/core/config.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

namespace FileCache {

// The cache folder: bin/cache next to the executable, except inside a macOS app bundle (where SDL's base path is the
// bundle's Contents/Resources), which should not be written to (it may be read-only, and writing breaks its
// signature): there it is the user's preferences folder for the game
// (~/Library/Application Support/modular-engine/<App name>/cache)
inline const std::string &cacheRoot() {
  static const std::string root = [] {
    const std::string &base = EngineConfig::getBasePath();
    const size_t bundle = base.find(".app/Contents/");
    if (bundle != std::string::npos) {
      const size_t slash = base.rfind('/', bundle);
      const size_t nameStart = slash == std::string::npos ? 0 : slash + 1;
      const std::string appName = base.substr(nameStart, bundle - nameStart);
      if (char *preferences = SDL_GetPrefPath("modular-engine", appName.c_str())) {
        std::string path = std::string(preferences) + "cache/";
        SDL_free(preferences);
        return path;
      }
    }
    return EngineConfig::resolvePath("cache/");
  }();
  return root;
}

// Where entries for `key` live: <cache folder>/<folder>/<hash of key><extension>
inline std::string entryPath(const std::string &folder, const std::string &key, const char *extension) {
  uint64_t hash = 1469598103934665603ull; // FNV-1a
  for (unsigned char c : key) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  char name[40];
  std::snprintf(name, sizeof(name), "%016llx%s", static_cast<unsigned long long>(hash), extension);
  return cacheRoot() + folder + "/" + name;
}

// A key that names the same file however its path was written
inline std::string canonicalKey(const std::string &path) {
  std::error_code error;
  std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
  return error ? path : canonical.generic_string();
}

inline int64_t timeOf(const std::filesystem::file_time_type &time) {
  return static_cast<int64_t>(time.time_since_epoch().count());
}

// Size and modification time of a source file: an entry made from it is current while both match
struct FileStamp {
  uint64_t size = 0;
  int64_t time = 0;
  bool operator==(const FileStamp &other) const { return size == other.size && time == other.time; }
};

inline bool stampOf(const std::string &path, FileStamp &stamp) {
  std::error_code error;
  stamp.size = std::filesystem::file_size(path, error);
  if (error)
    return false;
  stamp.time = timeOf(std::filesystem::last_write_time(path, error));
  return !error;
}

// Writes into a temporary file and renames it over the entry when done, so a reader never sees half a file
class Writer {
public:
  explicit Writer(std::string path) : m_path(std::move(path)), m_temporary(m_path + ".tmp") {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(m_path).parent_path(), error);
    m_out.open(m_temporary, std::ios::binary | std::ios::trunc);
  }

  template <typename T> void value(const T &v) {
    static_assert(std::is_trivially_copyable_v<T>);
    m_out.write(reinterpret_cast<const char *>(&v), sizeof(T));
  }
  template <typename T> void array(const std::vector<T> &items) {
    static_assert(std::is_trivially_copyable_v<T>);
    value(static_cast<uint64_t>(items.size()));
    m_out.write(reinterpret_cast<const char *>(items.data()), static_cast<std::streamsize>(items.size() * sizeof(T)));
  }
  void string(const std::string &s) {
    value(static_cast<uint32_t>(s.size()));
    m_out.write(s.data(), static_cast<std::streamsize>(s.size()));
  }

  // Returns false (and leaves no entry) if anything failed to write
  bool commit() {
    bool ok = static_cast<bool>(m_out);
    m_out.close();
    std::error_code error;
    if (ok)
      std::filesystem::rename(m_temporary, m_path, error);
    if (!ok || error)
      std::filesystem::remove(m_temporary, error);
    return ok && !error;
  }

private:
  std::string m_path;
  std::string m_temporary;
  std::ofstream m_out;
};

class Reader {
public:
  explicit Reader(const std::string &path) : m_in(path, std::ios::binary) {}
  explicit operator bool() const { return static_cast<bool>(m_in); }

  template <typename T> bool value(T &v) {
    static_assert(std::is_trivially_copyable_v<T>);
    return static_cast<bool>(m_in.read(reinterpret_cast<char *>(&v), sizeof(T)));
  }
  template <typename T> bool array(std::vector<T> &items) {
    uint64_t count = 0;
    if (!value(count) || count > (1ull << 32))
      return false;
    items.resize(count);
    return static_cast<bool>(
        m_in.read(reinterpret_cast<char *>(items.data()), static_cast<std::streamsize>(count * sizeof(T))));
  }
  bool string(std::string &s) {
    uint32_t size = 0;
    if (!value(size) || size > (1u << 20))
      return false;
    s.resize(size);
    return static_cast<bool>(m_in.read(s.data(), size));
  }

private:
  std::ifstream m_in;
};

} // namespace FileCache
