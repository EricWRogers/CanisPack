#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace CanisPack
{
    struct BuildChoice
    {
        std::string id = "Release";
        std::string label = "Release";
        std::string preset;
    };

    std::vector<BuildChoice> BuildChoicesFromPresetList(const std::string& output);
    BuildChoice FindBuildChoice(const std::vector<BuildChoice>& choices, const std::string& id);
    std::filesystem::path GetBuildDirectory(const std::filesystem::path& workspace, const BuildChoice& choice);
    void PrepareBuildQuery(const std::filesystem::path& build);
    std::vector<std::string> ConfigureArguments(const std::filesystem::path& workspace, const BuildChoice& choice);
    std::string ReadBuildCache(const std::filesystem::path& build, const std::string& key);
    std::string BuildConfiguration(const std::filesystem::path& build);
    bool CanLaunchEditorBuild(const std::filesystem::path& build, std::string& error);
    // Resolve only the selected build's CMake artifacts, never a stale project binary.
    std::filesystem::path FindBuildExecutable(const std::filesystem::path& build,
                                            const std::string& filename,
                                            std::string& target);
}
