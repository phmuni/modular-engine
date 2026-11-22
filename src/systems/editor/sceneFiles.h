#pragma once
// What the editor's File menu decides about scene files, apart from the window so it can be tested: the name Save As
// accepts and the list of recent scenes.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sceneFiles {

constexpr size_t kMaxRecent = 8;

// The file name Save As writes for what was typed ("level" and "level.json" give "level.json"). Empty, with the
// reason in `error`, when it is not a plain name: path separators, "..", a drive colon, characters files cannot
// have, or nothing but an extension. Scenes are only saved in assets/scenes, so a name never reaches elsewhere.
std::string fileName(std::string_view input, std::string *error = nullptr);

// Puts `path` first in the list: an earlier entry for the same file goes, and only the newest `max` stay
void pushRecent(std::vector<std::string> &recent, const std::string &path, size_t max = kMaxRecent);

} // namespace sceneFiles
