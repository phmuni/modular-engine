#include "systems/editor/sceneFiles.h"

#include <algorithm>
#include <cctype>

namespace sceneFiles {

std::string fileName(std::string_view input, std::string *error) {
  auto fail = [&](const char *reason) {
    if (error)
      *error = reason;
    return std::string();
  };
  size_t begin = 0, end = input.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(input[begin])))
    ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])))
    --end;
  std::string name(input.substr(begin, end - begin));
  std::string tail = name.size() >= 5 ? name.substr(name.size() - 5) : std::string();
  std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return std::tolower(c); });
  const bool hasExtension = tail == ".json";
  if (!hasExtension)
    while (!name.empty() && name.back() == '.')
      name.pop_back(); // Windows drops trailing dots, so the file would not have the name that was shown
  if (name.empty())
    return fail("Type a file name");
  if (name.find("..") != std::string::npos)
    return fail("\"..\" is not allowed in the name");
  for (char c : name) {
    if (c == '/' || c == '\\' || c == ':')
      return fail("The name cannot have folders: scenes are saved in assets/scenes");
    if (static_cast<unsigned char>(c) < 32 || std::string_view("<>\"|?*").find(c) != std::string_view::npos)
      return fail("The name has a character files cannot have");
  }
  if (name.front() == '.')
    return fail("The name cannot start with a dot");
  if (!hasExtension)
    name += ".json";
  if (name.size() == 5)
    return fail("Type a file name");
  return name;
}

void pushRecent(std::vector<std::string> &recent, const std::string &path, size_t max) {
  recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
  recent.insert(recent.begin(), path);
  if (recent.size() > max)
    recent.resize(max);
}

} // namespace sceneFiles
