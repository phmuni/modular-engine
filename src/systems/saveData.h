#pragma once
// Saved data: a small key-value store for a game's progress (level reached, best score) and settings (volumes).
//
// It lives in a JSON file, save.json, in the user's preferences folder for the game (set the name with
// Engine::setGameName before reading anything). Values are numbers, booleans, strings or structured JSON. Changes
// stay in memory until write(); the engine also writes them when the game quits. The file is replaced atomically (a
// temp file, then a rename), so a crash while saving cannot leave half a file. Deterministic runs (--deterministic)
// keep the data in memory only: they neither read nor write the file, so regression tests stay repeatable.

#include "foundation/ecs/systemManager.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

class SaveData : public BaseSystem {
public:
  SaveData();
  ~SaveData() override;

  // The game the data belongs to. The file is read the first time data is used after this.
  void setGameName(std::string_view name);
  // Memory only: no file is read or written
  void setInMemory(bool inMemory);
  // Keeps save.json in this folder (created if needed) instead of the preferences folder, for tests that must not touch
  // the player's real saves. Call after setGameName and before reading anything; it also ends memory-only mode.
  void setFolder(const std::string &folder);
  // The save file's path, empty while the data is in memory only
  std::string getPath();

  void set(std::string_view key, double value);
  void set(std::string_view key, int value);
  void set(std::string_view key, bool value);
  void set(std::string_view key, std::string_view value);
  void set(std::string_view key, const char *value);
  // Structured values: objects, arrays and nested combinations
  void setJson(std::string_view key, const nlohmann::json &value);

  // The fallback is returned when the key is missing or holds another kind of value
  double getNumber(std::string_view key, double fallback = 0.0);
  bool getBool(std::string_view key, bool fallback = false);
  std::string getString(std::string_view key, const std::string &fallback = "");
  // null when the key is missing
  nlohmann::json getJson(std::string_view key);

  bool has(std::string_view key);
  void remove(std::string_view key);
  void clear();

  // Saves now (when there is a file to save to); returns false if it failed
  bool write();
  // Saves only if something changed since the last write: what the engine does when the game quits
  void writeIfChanged();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
