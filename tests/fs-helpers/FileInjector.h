#pragma once
#include <filesystem>
#include <string>

/** @short Represents a temporary file whose lifetime is bound by lifetime of the FileInjector instance */
class FileInjector {
private:
    const std::string path;

public:
    FileInjector(const std::filesystem::path& path, const std::filesystem::perms permissions, const std::string& content);
    ~FileInjector() noexcept(false);
    void setPermissions(const std::filesystem::perms permissions);
};
