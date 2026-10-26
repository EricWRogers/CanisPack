#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace CanisPack
{
    // Missing or invalid settings do not mask another configuration's name.
    std::optional<std::string> ReadProjectExecutableName(const std::filesystem::path& configPath);

    std::filesystem::path GetBuiltEditorExecutable(const std::filesystem::path& workspacePath);
    std::filesystem::path GetProjectEditorExecutable(const std::filesystem::path& projectPath);
}
