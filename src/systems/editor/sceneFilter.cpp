#include "systems/editor/sceneFilter.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace sceneFilter {

std::string lower(const std::string &text) {
  std::string result = text;
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
  return result;
}

Query parse(const std::string &input) {
  Query query;
  std::istringstream words(lower(input));
  std::string word;
  while (words >> word) {
    if (word.rfind("t:", 0) == 0) {
      if (word.size() > 2) // "t:" alone is a term still being typed
        query.components.push_back(word.substr(2));
    } else if (word.rfind("tag:", 0) == 0) {
      if (word.size() > 4)
        query.tags.push_back(word.substr(4));
    } else {
      query.text.push_back(word);
    }
  }
  return query;
}

const std::vector<std::string> &builtInComponentNames() {
  static const std::vector<std::string> names = {"Model",       "RigidBody",  "CharacterController", "Light",
                                                 "Camera",      "Animator",   "AnimationStateMachine", "Script",
                                                 "Transform",   "AudioSource", "Skybox",             "Collision",
                                                 "ParticleEmitter", "Hidden", "CameraRig",           "Tags", "Joint"};
  return names;
}

bool matches(const Query &query, const std::string &name, const std::function<bool(const std::string &)> &hasComponent,
             const std::function<bool(const std::string &)> &hasTag) {
  if (!query.text.empty()) {
    const std::string lowered = lower(name);
    for (const std::string &fragment : query.text)
      if (lowered.find(fragment) == std::string::npos)
        return false;
  }
  for (const std::string &component : query.components)
    if (!hasComponent(component))
      return false;
  for (const std::string &tag : query.tags)
    if (!hasTag(tag))
      return false;
  return true;
}

} // namespace sceneFilter
