#include <fstream>
#include "FileInjector.h"

/** @short Creates a file with specific permissions and content */
FileInjector::FileInjector(const std::filesystem::path& path, const std::filesystem::perms permissions, const std::string& content)
    : path(path)
{
    auto fileStream = std::ofstream(path, std::ios_base::out | std::ios_base::trunc);
    if (!fileStream.is_open()) {
        throw std::invalid_argument("FileInjector could not open file " + std::string(path) + " for writing");
    }
    fileStream << content;
    std::filesystem::permissions(path, permissions);
}

/** @short Removes file associated with this FileInjector instance (if exists) */
FileInjector::~FileInjector() noexcept(false)
{
    std::filesystem::remove(path);
}

/** @short Sets file permissions */
void FileInjector::setPermissions(const std::filesystem::perms permissions)
{
    std::filesystem::permissions(path, permissions);
}
