// Saved data in a temporary folder: set, get, write, read back. Tests run with the data in memory (deterministic mode), so
// the player's real save folder is never touched; these point it at a temporary one on purpose.

#include "foundation/core/engine.h"
#include "systems/saveData.h"
#include "testing.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

TEST_CASE(save_in_memory_by_default) {
  // --deterministic keeps the data out of the preferences folder: no path, and a write does nothing
  CHECK(engine.saveData().getPath().empty());
  engine.saveData().set("score", 5);
  CHECK(engine.saveData().write());
  CHECK_NEAR(static_cast<float>(engine.saveData().getNumber("score")), 5.0f, 1e-6f);
}

TEST_CASE(save_to_folder) {
  const std::string folder = testing::tempDir("save");
  SaveData &save = engine.saveData();
  save.setGameName("EngineTestGame");
  save.setFolder(folder);
  const std::string file = folder + "/save.json";
  CHECK_EQ(save.getPath(), file);
  CHECK(!std::filesystem::exists(file)); // nothing written yet

  save.set("score", 120);
  save.set("ratio", 0.25);
  save.set("player", "Ada");
  save.set("muted", true);
  save.setJson("inventory", nlohmann::json::parse(R"({"items": ["key", "map"], "gold": 12})"));
  CHECK(!std::filesystem::exists(file)); // changes stay in memory until write()
  CHECK(save.has("score"));
  CHECK(!save.has("nothing"));
  CHECK_NEAR(static_cast<float>(save.getNumber("nothing", -1.0)), -1.0f, 1e-6f);

  CHECK(save.write());
  CHECK(std::filesystem::exists(file));
  CHECK(!std::filesystem::exists(file + ".tmp")); // replaced atomically: no leftover temp file

  // What is on disk (read and closed: an open file cannot be replaced on Windows, so the next write would fail)
  const nlohmann::json onDisk = [&] {
    std::ifstream stream(file);
    return nlohmann::json::parse(stream);
  }();
  CHECK_EQ(onDisk["score"].get<int>(), 120);
  CHECK_EQ(onDisk["player"].get<std::string>(), std::string("Ada"));
  CHECK_EQ(onDisk["inventory"]["items"][1].get<std::string>(), std::string("map"));

  // A fresh store reading the same folder sees it all (what the next run of the game does)
  SaveData next;
  next.setGameName("EngineTestGame");
  next.setFolder(folder);
  CHECK_NEAR(static_cast<float>(next.getNumber("score")), 120.0f, 1e-6f);
  CHECK_NEAR(static_cast<float>(next.getNumber("ratio")), 0.25f, 1e-6f);
  CHECK_EQ(next.getString("player"), std::string("Ada"));
  CHECK(next.getBool("muted"));
  CHECK_EQ(next.getJson("inventory")["gold"].get<int>(), 12);
  CHECK(next.getJson("missing").is_null());
  // The wrong kind of value gives the fallback
  CHECK_EQ(next.getString("score", "fallback"), std::string("fallback"));
  CHECK(!next.getBool("player"));

  // remove and clear
  next.remove("score");
  CHECK(!next.has("score"));
  CHECK(next.write());
  SaveData third;
  third.setFolder(folder);
  CHECK(!third.has("score"));
  CHECK(third.has("player"));
  third.clear();
  CHECK(third.write());
  SaveData fourth;
  fourth.setFolder(folder);
  CHECK(!fourth.has("player"));

  // writeIfChanged: what the engine does at quit; nothing changed, so the file is left as it is
  const auto before = std::filesystem::last_write_time(file);
  fourth.writeIfChanged();
  CHECK(std::filesystem::last_write_time(file) == before);
}

TEST_CASE(save_corrupt_file) {
  const std::string folder = testing::tempDir("save");
  {
    std::ofstream broken(folder + "/save.json");
    broken << "{ this is not json";
  }
  SaveData save;
  save.setFolder(folder);
  CHECK(!save.has("anything")); // starts empty...
  save.set("fresh", 1);
  CHECK(save.write());
  // ...and the unreadable file is kept for a look rather than overwritten
  CHECK(std::filesystem::exists(folder + "/save.json.corrupt"));
  SaveData again;
  again.setFolder(folder);
  CHECK(again.has("fresh"));
}
