#pragma once
// The search box of the editor's scene list: "wheel", "t:Light", "tag:enemy", or several of them separated by spaces
// (an entity must match all). Kept apart from the panel so it can be tested without a window.

#include <functional>
#include <string>
#include <vector>

namespace sceneFilter {

// What was typed, lowercased: name fragments, component names (t:) and tags (tag:)
struct Query {
  std::vector<std::string> text;
  std::vector<std::string> components;
  std::vector<std::string> tags;
  bool empty() const { return text.empty() && components.empty() && tags.empty(); }
};

Query parse(const std::string &input);

// The component names the editor can look an entity up by (Engine::hasComponent); `t:` accepts them in any case
const std::vector<std::string> &builtInComponentNames();

// True when the entity named `name` fits every term. `hasComponent` and `hasTag` get the term lowercased.
bool matches(const Query &query, const std::string &name, const std::function<bool(const std::string &)> &hasComponent,
             const std::function<bool(const std::string &)> &hasTag);

std::string lower(const std::string &text);

} // namespace sceneFilter
