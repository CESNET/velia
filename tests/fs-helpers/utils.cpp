#include "utils.h"

/** @short Remove directory tree at 'rootDir' path (if exists) */
void removeDirectoryTreeIfExists(const std::filesystem::path& rootDir)
{
    if (std::filesystem::exists(rootDir)) {
        std::filesystem::remove_all(rootDir);
    }
}
