// The player's options: the registry of settings and how they are applied, kept and restored. See playerSettings.h.

#include "systems/playerSettings.h"

#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "rendering/renderer.h"
#include "systems/audioSystem.h"
#include "systems/saveData.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>

namespace {
using Value = PlayerSettings::Value;
using Kind = PlayerSettings::Kind;
using Range = PlayerSettings::Range;

struct Choice {
  Value value;
  std::string label;
};

// What one option is and how it reaches the engine and the save data. The functions after saveKey are defaults filled
// in by add() when an entry leaves them out.
struct Setting {
  std::string name;
  Kind kind = Kind::Bool;
  Range range;
  std::function<std::vector<Choice>()> choices;     // a list: the values and their texts (may change: window sizes)
  std::function<Value()> read;                      // from the engine
  std::function<void(const Value &)> apply;         // onto the engine
  std::function<std::string(const Value &)> format; // the text of a number or a boolean
  std::function<bool()> enabled;                    // empty: always
  std::string saveKey;                              // the "engine.*" key in the save data
  std::function<void(SaveData &, const Value &)> save;     // empty: the value under saveKey
  std::function<void(SaveData &)> unsave;                  // empty: saveKey removed
  std::function<std::optional<Value>(SaveData &)> restore; // empty: the value under saveKey
};

double asNumber(const Value &value) {
  if (const double *number = std::get_if<double>(&value))
    return *number;
  if (const bool *flag = std::get_if<bool>(&value))
    return *flag ? 1.0 : 0.0;
  return std::strtod(std::get<std::string>(value).c_str(), nullptr);
}
bool asBool(const Value &value) {
  if (const bool *flag = std::get_if<bool>(&value))
    return *flag;
  if (const double *number = std::get_if<double>(&value))
    return *number != 0.0;
  const std::string &text = std::get<std::string>(value);
  return text == "true" || text == "on" || text == "On" || text == "1";
}
std::string asString(const Value &value) {
  if (const std::string *text = std::get_if<std::string>(&value))
    return *text;
  if (const bool *flag = std::get_if<bool>(&value))
    return *flag ? "true" : "false";
  char text[32];
  std::snprintf(text, sizeof(text), "%g", std::get<double>(value));
  return text;
}

bool sameValue(const Value &a, const Value &b) {
  if (a.index() != b.index())
    return false;
  if (const double *number = std::get_if<double>(&a))
    return std::fabs(*number - std::get<double>(b)) < 1e-9;
  return a == b;
}

// A number clamped to its range and rounded to its step; written the way a person would (0.49, not 0.4900000001)
double snap(double value, const Range &range) {
  value = std::clamp(value, range.min, range.max);
  if (range.step <= 0.0)
    return value;
  const double inverse = std::round(1.0 / range.step);
  if (std::fabs(inverse * range.step - 1.0) < 1e-9)
    return std::clamp(std::round(value * inverse) / inverse, range.min, range.max);
  return std::clamp(range.min + std::round((value - range.min) / range.step) * range.step, range.min, range.max);
}

std::string percentText(double value) { return std::to_string(static_cast<int>(std::lround(value * 100.0))) + "%"; }
std::string twoDecimals(double value) {
  char text[16];
  std::snprintf(text, sizeof(text), "%.2f", value);
  return text;
}
std::string onOff(const Value &value) { return asBool(value) ? "On" : "Off"; }

std::string sizeText(const glm::ivec2 &size) { return std::to_string(size.x) + " x " + std::to_string(size.y); }
std::optional<glm::ivec2> parseSize(const std::string &text) {
  int width = 0, height = 0;
  if (std::sscanf(text.c_str(), "%d x %d", &width, &height) == 2 && width > 0 && height > 0)
    return glm::ivec2(width, height);
  return std::nullopt;
}

nlohmann::json toJson(const Value &value, Kind kind) {
  if (const bool *flag = std::get_if<bool>(&value))
    return *flag;
  if (const double *number = std::get_if<double>(&value)) {
    // A list's numbers (MSAA samples) are whole; a number is kept as it is
    if (kind == Kind::Choice && std::floor(*number) == *number)
      return static_cast<long long>(*number);
    return *number;
  }
  return std::get<std::string>(value);
}
} // namespace

struct PlayerSettings::Impl {
  explicit Impl(Engine &e) : engine(e) { registerSettings(); }

  Engine &engine;
  std::vector<Setting> settings;
  std::map<std::string, Value> defaults;
  std::set<std::string> appliedFromSave; // what the save data changed, to put back if the game's save changes
  bool defaultsCaptured = false;

  AudioSystem &audio() { return engine.getSystemManager().getSystem<AudioSystem>(); }
  SaveData &save() { return engine.saveData(); }

  const Setting *find(const std::string &name) const {
    for (const Setting &setting : settings)
      if (setting.name == name)
        return &setting;
    return nullptr;
  }

  // --- The registry: one entry per option. A new option is a new call here. ----------------------------------------

  void add(Setting setting) {
    if (!setting.save)
      setting.save = [key = setting.saveKey, kind = setting.kind](SaveData &data, const Value &value) {
        data.setJson(key, toJson(value, kind));
      };
    if (!setting.unsave)
      setting.unsave = [key = setting.saveKey](SaveData &data) { data.remove(key); };
    if (!setting.restore)
      setting.restore = [key = setting.saveKey](SaveData &data) -> std::optional<Value> {
        if (!data.has(key))
          return std::nullopt;
        const nlohmann::json json = data.getJson(key);
        if (json.is_boolean())
          return Value(json.get<bool>());
        if (json.is_number())
          return Value(json.get<double>());
        if (json.is_string())
          return Value(json.get<std::string>());
        return std::nullopt;
      };
    settings.push_back(std::move(setting));
  }

  void addVolume(const char *name, const char *key, std::function<float()> read, std::function<void(float)> apply) {
    Setting setting;
    setting.name = name;
    setting.kind = Kind::Number;
    setting.range = {0.0, 1.0, 0.01};
    setting.saveKey = key;
    setting.read = [read] { return Value(static_cast<double>(read())); };
    setting.apply = [apply](const Value &value) { apply(static_cast<float>(asNumber(value))); };
    setting.format = [](const Value &value) { return percentText(asNumber(value)); };
    add(std::move(setting));
  }

  void addToggle(const char *name, const char *key, std::function<bool()> read, std::function<void(bool)> apply) {
    Setting setting;
    setting.name = name;
    setting.kind = Kind::Bool;
    setting.saveKey = key;
    setting.read = [read] { return Value(read()); };
    setting.apply = [apply](const Value &value) { apply(asBool(value)); };
    setting.format = onOff;
    add(std::move(setting));
  }

  void registerSettings() {
    addVolume("masterVolume", "engine.volume.master", [this] { return audio().getMasterVolume(); },
              [this](float volume) { audio().setMasterVolume(volume); });
    addVolume("musicVolume", "engine.volume.music", [this] { return audio().getBusVolume(AudioSystem::Bus::Music); },
              [this](float volume) { audio().setBusVolume(AudioSystem::Bus::Music, volume); });
    addVolume("effectsVolume", "engine.volume.effects",
              [this] { return audio().getBusVolume(AudioSystem::Bus::Effects); },
              [this](float volume) { audio().setBusVolume(AudioSystem::Bus::Effects, volume); });

    // The window's size: the 16:9 sizes that fit the display, and the window's own when it is not one of them (it was
    // dragged). Kept as the two keys the engine also remembers the window's place under (Engine::startWindow).
    {
      Setting setting;
      setting.name = "resolution";
      setting.kind = Kind::Choice;
      setting.saveKey = "engine.window.width";
      setting.choices = [this] {
        std::vector<glm::ivec2> sizes = engine.resolutions();
        const glm::ivec2 current = engine.windowSize();
        if (std::find(sizes.begin(), sizes.end(), current) == sizes.end())
          sizes.push_back(current);
        std::sort(sizes.begin(), sizes.end(), [](const glm::ivec2 &a, const glm::ivec2 &b) {
          return static_cast<long long>(a.x) * a.y < static_cast<long long>(b.x) * b.y;
        });
        std::vector<Choice> choices;
        for (const glm::ivec2 &size : sizes)
          choices.push_back({Value(sizeText(size)), sizeText(size)});
        return choices;
      };
      setting.read = [this] { return Value(sizeText(engine.windowSize())); };
      setting.apply = [this](const Value &value) {
        if (engine.isFullscreen()) // the size is the display's then; the window keeps its own for when it comes back
          return;
        if (const auto size = parseSize(asString(value)))
          engine.setWindowSize(size->x, size->y);
      };
      setting.enabled = [this] { return !engine.isFullscreen(); };
      setting.save = [](SaveData &data, const Value &value) {
        if (const auto size = parseSize(asString(value))) {
          data.set("engine.window.width", size->x);
          data.set("engine.window.height", size->y);
        }
      };
      setting.unsave = [](SaveData &data) {
        data.remove("engine.window.width");
        data.remove("engine.window.height");
      };
      // Applied with the window's place, when it is shown (Engine::startWindow)
      setting.restore = [](SaveData &) -> std::optional<Value> { return std::nullopt; };
      add(std::move(setting));
    }

    {
      Setting setting;
      setting.name = "renderScale";
      setting.kind = Kind::Number;
      setting.range = {Renderer::kMinRenderScale, Renderer::kMaxRenderScale, 0.05};
      setting.saveKey = "engine.renderScale";
      setting.read = [this] { return Value(static_cast<double>(engine.renderScale())); };
      setting.apply = [this](const Value &value) { engine.setRenderScale(static_cast<float>(asNumber(value))); };
      setting.format = [](const Value &value) { return percentText(asNumber(value)); };
      add(std::move(setting));
    }

    {
      Setting setting;
      setting.name = "msaa";
      setting.kind = Kind::Choice;
      setting.saveKey = "engine.msaa";
      setting.choices = [] {
        return std::vector<Choice>{
            {Value(0.0), "Off"}, {Value(2.0), "MSAA 2x"}, {Value(4.0), "MSAA 4x"}, {Value(8.0), "MSAA 8x"}};
      };
      setting.read = [this] { return Value(static_cast<double>(engine.msaa())); };
      setting.apply = [this](const Value &value) { engine.setMsaa(static_cast<int>(asNumber(value))); };
      add(std::move(setting));
    }

    addToggle("fxaa", "engine.fxaa", [this] { return engine.fxaa(); }, [this](bool on) { engine.setFxaa(on); });
    addToggle("fsr", "engine.fsr", [this] { return engine.fsr(); }, [this](bool on) { engine.setFsr(on); });

    {
      Setting setting;
      setting.name = "fsrSharpness";
      setting.kind = Kind::Number;
      setting.range = {0.0, 2.0, 0.05};
      setting.saveKey = "engine.fsrSharpness";
      setting.read = [this] { return Value(static_cast<double>(engine.fsrSharpness())); };
      setting.apply = [this](const Value &value) { engine.setFsrSharpness(static_cast<float>(asNumber(value))); };
      setting.format = [](const Value &value) { return twoDecimals(asNumber(value)); };
      // FSR sharpens the upscale: it matters while FSR is on and the scene is drawn below full size
      setting.enabled = [this] { return engine.fsr() && engine.renderScale() < 1.0f - 1e-4f; };
      add(std::move(setting));
    }

    addToggle("ssao", "engine.ssao", [this] { return engine.ssao(); }, [this](bool on) { engine.setSsao(on); });
    {
      Setting setting;
      setting.name = "ssaoQuality";
      setting.kind = Kind::Choice;
      setting.saveKey = "engine.ssaoQuality";
      setting.choices = [] {
        return std::vector<Choice>{{Value(std::string("low")), "Low"},
                                   {Value(std::string("medium")), "Medium"},
                                   {Value(std::string("high")), "High"}};
      };
      setting.read = [this] { return Value(std::string(ssaoQualityName(engine.ssaoSettings().quality))); };
      setting.apply = [this](const Value &value) {
        if (const auto quality = ssaoQualityFromName(asString(value))) {
          SsaoSettings current = engine.ssaoSettings();
          current.quality = *quality;
          engine.setSsaoSettings(current);
        }
      };
      setting.enabled = [this] { return engine.ssao(); };
      add(std::move(setting));
    }

    addToggle("fullscreen", "engine.fullscreen", [this] { return engine.isFullscreen(); },
              [this](bool on) { engine.setFullscreen(on); });
    addToggle("vsync", "engine.vsync", [this] { return engine.getState(Toggle::VSync); },
              [this](bool on) { engine.setState(Toggle::VSync, on); });
  }

  // --- Values ------------------------------------------------------------------------------------------------------

  std::vector<Choice> choicesOf(const Setting &setting) const {
    return setting.choices ? setting.choices() : std::vector<Choice>{};
  }

  // What was asked for, as a value this setting holds; nothing when it does not fit
  std::optional<Value> coerce(const Setting &setting, const Value &requested) const {
    switch (setting.kind) {
    case Kind::Bool:
      return Value(asBool(requested));
    case Kind::Number:
      if (std::holds_alternative<bool>(requested))
        return std::nullopt;
      if (const std::string *text = std::get_if<std::string>(&requested)) {
        char *end = nullptr;
        std::strtod(text->c_str(), &end);
        if (text->empty() || *end != '\0')
          return std::nullopt;
      }
      return Value(snap(asNumber(requested), setting.range));
    case Kind::Choice: {
      const std::vector<Choice> choices = choicesOf(setting);
      for (const Choice &choice : choices)
        if (sameValue(choice.value, requested))
          return choice.value;
      if (const std::string *text = std::get_if<std::string>(&requested))
        for (const Choice &choice : choices)
          if (choice.label == *text)
            return choice.value;
      if (std::holds_alternative<double>(requested)) { // numbers snap down, as the engine does for MSAA
        const double wanted = std::get<double>(requested);
        const Choice *best = nullptr;
        for (const Choice &choice : choices) {
          if (!std::holds_alternative<double>(choice.value))
            continue;
          const double number = std::get<double>(choice.value);
          if (number <= wanted && (!best || number > std::get<double>(best->value)))
            best = &choice;
        }
        if (!best) // below the lowest: the lowest
          for (const Choice &choice : choices)
            if (std::holds_alternative<double>(choice.value) &&
                (!best || std::get<double>(choice.value) < std::get<double>(best->value)))
              best = &choice;
        if (best)
          return best->value;
      }
      return std::nullopt;
    }
    }
    return std::nullopt;
  }

  Value current(const Setting &setting) const {
    Value value = setting.read();
    if (setting.kind == Kind::Number)
      return Value(snap(asNumber(value), setting.range));
    return value;
  }

  void persist(const Setting &setting) { setting.save(save(), current(setting)); }

  // --- Defaults and the save data ----------------------------------------------------------------------------------

  void captureDefaults() {
    if (defaultsCaptured)
      return;
    defaultsCaptured = true;
    for (const Setting &setting : settings)
      defaults[setting.name] = setting.read();
  }

  void applyDefault(const Setting &setting) {
    captureDefaults();
    setting.apply(defaults[setting.name]);
  }

  void applySaved() {
    captureDefaults();
    SaveData &data = save();
    for (const Setting &setting : settings) {
      const std::optional<Value> saved = setting.restore(data);
      const std::optional<Value> value = saved ? coerce(setting, *saved) : std::nullopt;
      if (value) {
        setting.apply(*value);
        appliedFromSave.insert(setting.name);
      } else if (appliedFromSave.erase(setting.name)) {
        applyDefault(setting); // the game's save changed (setGameName): this one is not in the new one
      }
    }
  }
};

PlayerSettings::PlayerSettings(Engine &engine) : m_impl(std::make_unique<Impl>(engine)) {}
PlayerSettings::~PlayerSettings() = default;

std::vector<std::string> PlayerSettings::names() const {
  std::vector<std::string> names;
  for (const Setting &setting : m_impl->settings)
    names.push_back(setting.name);
  return names;
}

bool PlayerSettings::has(const std::string &name) const { return m_impl->find(name) != nullptr; }

PlayerSettings::Kind PlayerSettings::kind(const std::string &name) const {
  const Setting *setting = m_impl->find(name);
  return setting ? setting->kind : Kind::Bool;
}

PlayerSettings::Range PlayerSettings::range(const std::string &name) const {
  const Setting *setting = m_impl->find(name);
  return setting && setting->kind == Kind::Number ? setting->range : Range{0.0, 0.0, 0.0};
}

PlayerSettings::Value PlayerSettings::get(const std::string &name) {
  const Setting *setting = m_impl->find(name);
  return setting ? m_impl->current(*setting) : Value(false);
}

bool PlayerSettings::set(const std::string &name, const Value &value) {
  const Setting *setting = m_impl->find(name);
  if (!setting)
    return false;
  const std::optional<Value> coerced = m_impl->coerce(*setting, value);
  if (!coerced) {
    LOG_W("[PlayerSettings] %s: that value does not fit", name.c_str());
    return false;
  }
  m_impl->captureDefaults();
  setting->apply(*coerced);
  m_impl->persist(*setting); // what took effect (the GPU may cap MSAA), not what was asked for
  return true;
}

bool PlayerSettings::step(const std::string &name, int direction) {
  const Setting *setting = m_impl->find(name);
  if (!setting || direction == 0)
    return false;
  const int sign = direction > 0 ? 1 : -1;
  switch (setting->kind) {
  case Kind::Bool:
    return set(name, Value(!asBool(get(name))));
  case Kind::Number: {
    const double unit =
        setting->range.step > 0.0 ? setting->range.step : (setting->range.max - setting->range.min) / 20.0;
    return set(name, Value(asNumber(get(name)) + sign * unit));
  }
  case Kind::Choice: {
    const std::vector<Choice> choices = m_impl->choicesOf(*setting);
    const int count = static_cast<int>(choices.size());
    if (count == 0)
      return false;
    m_impl->captureDefaults();
    const Value before = setting->read();
    int index = -1;
    for (int i = 0; i < count; ++i)
      if (sameValue(choices[i].value, before))
        index = i;
    if (index < 0) // not on the list: the first one going up, the last going down
      index = sign > 0 ? -1 : count;
    // The next one the engine takes: a level the GPU does not have leaves things as they were, so on to the next
    for (int i = 1; i <= count; ++i) {
      const int next = ((index + sign * i) % count + count) % count;
      setting->apply(choices[next].value);
      if (!sameValue(setting->read(), before))
        break;
    }
    m_impl->persist(*setting);
    return true;
  }
  }
  return false;
}

std::vector<std::string> PlayerSettings::choices(const std::string &name) {
  std::vector<std::string> labels;
  if (const Setting *setting = m_impl->find(name))
    for (const Choice &choice : m_impl->choicesOf(*setting))
      labels.push_back(choice.label);
  return labels;
}

std::string PlayerSettings::label(const std::string &name) {
  const Setting *setting = m_impl->find(name);
  if (!setting)
    return "";
  const Value value = m_impl->current(*setting);
  if (setting->kind == Kind::Choice) {
    for (const Choice &choice : m_impl->choicesOf(*setting))
      if (sameValue(choice.value, value))
        return choice.label;
    return asString(value);
  }
  return setting->format ? setting->format(value) : asString(value);
}

bool PlayerSettings::enabled(const std::string &name) {
  const Setting *setting = m_impl->find(name);
  return setting && (!setting->enabled || setting->enabled());
}

void PlayerSettings::reset(const std::string &name) {
  const Setting *setting = m_impl->find(name);
  if (!setting)
    return;
  m_impl->applyDefault(*setting);
  setting->unsave(m_impl->save());
  m_impl->appliedFromSave.erase(name);
}

void PlayerSettings::resetAll() {
  for (const Setting &setting : m_impl->settings)
    reset(setting.name);
}

void PlayerSettings::captureDefaults() { m_impl->captureDefaults(); }
void PlayerSettings::applySaved() { m_impl->applySaved(); }
