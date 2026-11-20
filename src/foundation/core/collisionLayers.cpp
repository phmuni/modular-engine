// Collision layers: the names and the matrix.

#include "foundation/core/collisionLayers.h"

#include "foundation/core/logger.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

void CollisionLayers::reset() {
  m_names = {kDefaultLayer, "static", "player", "enemy", "projectile", "trigger"};
  std::fill(std::begin(m_masks), std::end(m_masks), kAllLayers);
  update();
}

void CollisionLayers::update() {
  const uint32_t used = count() >= kMaxLayers ? kAllLayers : (1u << count()) - 1u;
  m_allCollide = true;
  for (int i = 0; i < count(); ++i)
    if ((m_masks[i] & used) != used)
      m_allCollide = false;
  ++m_version;
}

int CollisionLayers::indexOf(std::string_view name) const {
  for (int i = 0; i < count(); ++i)
    if (m_names[static_cast<size_t>(i)] == name)
      return i;
  return -1;
}

int CollisionLayers::resolve(std::string_view name) const {
  const int index = indexOf(name);
  return index < 0 ? 0 : index;
}

int CollisionLayers::add(std::string_view name) {
  if (name.empty())
    return -1;
  if (const int existing = indexOf(name); existing >= 0)
    return existing;
  if (count() >= kMaxLayers)
    return -1;
  m_names.emplace_back(name);
  m_masks[count() - 1] = kAllLayers;
  update();
  return count() - 1;
}

bool CollisionLayers::remove(std::string_view name) {
  const int index = indexOf(name);
  if (index < kBuiltInLayers)
    return false;
  // Later layers move down one place, in the rows of the matrix and in the bits of every row
  for (int i = index; i + 1 < count(); ++i)
    m_masks[i] = m_masks[i + 1];
  const uint32_t below = (1u << index) - 1u;
  for (int i = 0; i + 1 < count(); ++i)
    m_masks[i] = (m_masks[i] & below) | ((m_masks[i] >> 1) & ~below);
  m_masks[count() - 1] = kAllLayers;
  m_names.erase(m_names.begin() + index);
  update();
  return true;
}

bool CollisionLayers::collide(std::string_view a, std::string_view b) const { return collide(resolve(a), resolve(b)); }

bool CollisionLayers::setCollide(std::string_view a, std::string_view b, bool collide) {
  const int first = indexOf(a), second = indexOf(b);
  if (first < 0 || second < 0)
    return false;
  auto set = [&](int from, int to) {
    if (collide)
      m_masks[from] |= 1u << to;
    else
      m_masks[from] &= ~(1u << to);
  };
  set(first, second);
  set(second, first);
  update();
  return true;
}

uint32_t CollisionLayers::maskOf(const std::vector<std::string> &layers, std::vector<std::string> *unknown) const {
  uint32_t mask = 0;
  for (const std::string &layer : layers) {
    const int index = indexOf(layer);
    if (index >= 0)
      mask |= 1u << index;
    else if (unknown)
      unknown->push_back(layer);
  }
  return mask;
}

void CollisionLayers::fromJson(const nlohmann::json &json) {
  reset();
  merge(json);
}

void CollisionLayers::merge(const nlohmann::json &json) {
  if (!json.is_object())
    return;
  if (auto names = json.find("names"); names != json.end() && names->is_array())
    for (const auto &name : *names)
      if (name.is_string() && add(name.get<std::string>()) < 0)
        LOG_W("[Layers] Cannot define layer \"%s\": all %d are taken", name.get<std::string>().c_str(), kMaxLayers);
  if (auto ignore = json.find("ignore"); ignore != json.end() && ignore->is_array())
    for (const auto &pair : *ignore) {
      if (!pair.is_array() || pair.size() != 2 || !pair[0].is_string() || !pair[1].is_string() ||
          !setCollide(pair[0].get<std::string>(), pair[1].get<std::string>(), false))
        LOG_W("[Layers] Skipping a layer pair that does not name two layers: %s", pair.dump().c_str());
    }
}

nlohmann::json CollisionLayers::toJson() const {
  nlohmann::json json = nlohmann::json::object();
  nlohmann::json names = nlohmann::json::array();
  for (int i = kBuiltInLayers; i < count(); ++i)
    names.push_back(m_names[static_cast<size_t>(i)]);
  if (!names.empty())
    json["names"] = names;
  nlohmann::json ignore = nlohmann::json::array();
  for (int a = 0; a < count(); ++a)
    for (int b = a; b < count(); ++b)
      if (!collide(a, b))
        ignore.push_back({m_names[static_cast<size_t>(a)], m_names[static_cast<size_t>(b)]});
  if (!ignore.empty())
    json["ignore"] = ignore;
  return json;
}

bool CollisionLayers::isDefault() const { return count() == kBuiltInLayers && m_allCollide; }

bool CollisionLayers::saveToProjectFile(const std::string &path) const {
  nlohmann::json project;
  {
    std::ifstream in(path);
    if (!in)
      return false;
    try {
      project = nlohmann::json::parse(in);
    } catch (const std::exception &) {
      return false;
    }
  }
  if (!project.is_object())
    return false;
  if (isDefault())
    project.erase("layers");
  else
    project["layers"] = toJson();
  std::ofstream out(path);
  if (!out)
    return false;
  out << project.dump(2) << "\n";
  return static_cast<bool>(out);
}
