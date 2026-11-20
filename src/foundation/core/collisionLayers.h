#pragma once
// Collision layers: named groups of entities and a matrix saying which groups interact, so that bullets can pass the
// player or enemies stop pushing each other. Physics bodies, characters, Collision boxes and raycasts all read it.
//
// There are up to 32 layers: six built in ("default", "static", "player", "enemy", "projectile", "trigger"), which
// are always there, and up to 26 more a game defines. Everything starts on "default" and every pair collides, which
// is how things behaved before layers existed. Components keep their layer by name (RigidBody::layer and friends),
// so scene files do not depend on the order layers were defined in; a name nothing answers to counts as "default".
//
// A Lua project keeps its layers in project.json (see fromJson); C++ games call add and setCollide in setup. The
// matrix is symmetric: setCollide("a", "b", false) is the same as setCollide("b", "a", false).

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

class CollisionLayers {
public:
  static constexpr int kMaxLayers = 32;
  static constexpr int kBuiltInLayers = 6;
  static constexpr uint32_t kAllLayers = 0xFFFFFFFFu; // a layer mask with every layer in it
  static constexpr const char *kDefaultLayer = "default";

  CollisionLayers() { reset(); }

  // Back to the built-in layers, all colliding
  void reset();

  int count() const { return static_cast<int>(m_names.size()); }
  const std::string &name(int index) const { return m_names[static_cast<size_t>(index)]; }
  const std::vector<std::string> &names() const { return m_names; }
  // The layer's number, or -1 for a name no layer has
  int indexOf(std::string_view name) const;
  // The layer's number, with the default layer (0) for an empty or unknown name
  int resolve(std::string_view name) const;

  // Defines a layer (the number of the one already so named, if there is); -1 when all 32 are taken or the name is empty
  int add(std::string_view name);
  // Removes a layer a game defined (not a built-in one): its components fall back to "default"
  bool remove(std::string_view name);

  // Whether things on these layers interact
  bool collide(int a, int b) const { return ((m_masks[static_cast<size_t>(a)] >> b) & 1u) != 0; }
  // By name; one that is no layer counts as "default"
  bool collide(std::string_view a, std::string_view b) const;
  // False when either name is not a layer
  bool setCollide(std::string_view a, std::string_view b, bool collide);
  // Every pair collides: the state nothing was configured in, where checks can be skipped
  bool allCollide() const { return m_allCollide; }

  // The mask of the named layers (bit n is layer n), for raycasts. Names that are no layer are left out and, when
  // `unknown` is given, added to it.
  uint32_t maskOf(const std::vector<std::string> &layers, std::vector<std::string> *unknown = nullptr) const;

  // Changes whenever the layers or the matrix do, for what keeps a copy of the result (bodies)
  uint32_t version() const { return m_version; }

  // project.json's "layers": { "names": ["water"], "ignore": [["enemy", "enemy"]] } (the layers a game defines, and the
  // pairs that do not collide). Reading starts from the built-in layers.
  void fromJson(const nlohmann::json &json);
  nlohmann::json toJson() const;
  // Whether there is nothing to write: a game that never touched the layers has no "layers" in its project.json
  bool isDefault() const;
  // Writes `layers` into the project file, leaving the rest of it as it is (or taking the key out when default)
  bool saveToProjectFile(const std::string &path) const;

private:
  void update();

  std::vector<std::string> m_names;
  uint32_t m_masks[kMaxLayers] = {}; // per layer, the layers it collides with
  bool m_allCollide = true;
  uint32_t m_version = 0;
};
