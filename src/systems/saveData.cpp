// Saved data implementation: nlohmann::json in memory, a JSON file in the preferences folder on disk.

#include "systems/saveData.h"

#include "foundation/core/logger.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

struct SaveData::Impl {
  std::string gameName = "Game";
  bool inMemory = false;
  bool loaded = false;
  bool dirty = false;
  std::string path; // empty until the preferences folder was asked for
  nlohmann::json values = nlohmann::json::object();

  const std::string &filePath() {
    if (path.empty()) {
      // SDL creates the folder; the pointer is SDL's to free
      char *folder = SDL_GetPrefPath("modular-engine", gameName.c_str());
      if (folder) {
        path = std::string(folder) + "save.json";
        SDL_free(folder);
      } else {
        LOG_W("[Save] No preferences folder for %s: %s", gameName.c_str(), SDL_GetError());
      }
    }
    return path;
  }

  void load() {
    if (loaded)
      return;
    loaded = true;
    values = nlohmann::json::object();
    if (inMemory)
      return;
    const std::string &file = filePath();
    std::ifstream stream(file);
    if (file.empty() || !stream)
      return; // no save yet
    try {
      nlohmann::json parsed = nlohmann::json::parse(stream);
      if (!parsed.is_object())
        throw std::runtime_error("the top level is not an object");
      values = std::move(parsed);
    } catch (const std::exception &error) {
      // Keep the unreadable file for a look instead of overwriting it with the next write
      stream.close();
      std::error_code ignored;
      std::filesystem::rename(file, file + ".corrupt", ignored);
      LOG_W("[Save] %s could not be read (%s); starting empty, the old file is kept as save.json.corrupt",
            file.c_str(), error.what());
    }
  }

  template <typename T> void store(std::string_view key, T &&value) {
    load();
    values[std::string(key)] = std::forward<T>(value);
    dirty = true;
  }

  const nlohmann::json *find(std::string_view key) {
    load();
    auto it = values.find(std::string(key));
    return it == values.end() ? nullptr : &*it;
  }
};

SaveData::SaveData() : m_impl(std::make_unique<Impl>()) {}
SaveData::~SaveData() = default;

void SaveData::setGameName(std::string_view name) {
  Impl &impl = *m_impl;
  if (impl.gameName == name)
    return;
  impl.gameName = std::string(name);
  impl.path.clear();
  impl.loaded = false;
  impl.dirty = false;
}

void SaveData::setInMemory(bool inMemory) {
  Impl &impl = *m_impl;
  impl.inMemory = inMemory;
  impl.loaded = false;
  impl.dirty = false;
}

std::string SaveData::getPath() { return m_impl->inMemory ? std::string() : m_impl->filePath(); }

void SaveData::set(std::string_view key, double value) { m_impl->store(key, value); }
void SaveData::set(std::string_view key, int value) { m_impl->store(key, value); }
void SaveData::set(std::string_view key, bool value) { m_impl->store(key, value); }
void SaveData::set(std::string_view key, std::string_view value) { m_impl->store(key, std::string(value)); }
void SaveData::set(std::string_view key, const char *value) { m_impl->store(key, std::string(value)); }
void SaveData::setJson(std::string_view key, const nlohmann::json &value) { m_impl->store(key, value); }

double SaveData::getNumber(std::string_view key, double fallback) {
  const nlohmann::json *value = m_impl->find(key);
  return value && value->is_number() ? value->get<double>() : fallback;
}

bool SaveData::getBool(std::string_view key, bool fallback) {
  const nlohmann::json *value = m_impl->find(key);
  return value && value->is_boolean() ? value->get<bool>() : fallback;
}

std::string SaveData::getString(std::string_view key, const std::string &fallback) {
  const nlohmann::json *value = m_impl->find(key);
  return value && value->is_string() ? value->get<std::string>() : fallback;
}

nlohmann::json SaveData::getJson(std::string_view key) {
  const nlohmann::json *value = m_impl->find(key);
  return value ? *value : nlohmann::json();
}

bool SaveData::has(std::string_view key) { return m_impl->find(key) != nullptr; }

void SaveData::remove(std::string_view key) {
  Impl &impl = *m_impl;
  impl.load();
  if (impl.values.erase(std::string(key)) > 0)
    impl.dirty = true;
}

void SaveData::clear() {
  Impl &impl = *m_impl;
  impl.load();
  if (!impl.values.empty())
    impl.dirty = true;
  impl.values = nlohmann::json::object();
}

bool SaveData::write() {
  Impl &impl = *m_impl;
  if (impl.inMemory)
    return true;
  impl.load(); // so a write before any read does not replace the file with an empty one
  const std::string &file = impl.filePath();
  if (file.empty())
    return false;
  const std::string temporary = file + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::trunc);
    stream << impl.values.dump(2) << '\n';
    stream.flush();
    if (!stream) {
      LOG_W("[Save] Could not write %s", temporary.c_str());
      return false;
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, file, error); // replaces the old file in one step
  if (error) {
    LOG_W("[Save] Could not replace %s: %s", file.c_str(), error.message().c_str());
    return false;
  }
  impl.dirty = false;
  return true;
}

void SaveData::writeIfChanged() {
  if (m_impl->dirty)
    write();
}
