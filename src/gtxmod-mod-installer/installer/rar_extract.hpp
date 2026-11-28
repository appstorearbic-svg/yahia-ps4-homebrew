#pragma once

#include <string>

namespace gtxinstall {

// Extract a single-volume, unencrypted RAR archive into an existing staging directory.
// Archive paths are checked before UnRAR writes any file.
bool extractRarArchive(const std::string &archivePath,
                       const std::string &destination,
                       std::string *error);

} // namespace gtxinstall
