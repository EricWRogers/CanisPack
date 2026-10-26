#include "ProjectExecutable.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <system_error>

namespace CanisPack
{
    namespace fs = std::filesystem;

    namespace
    {
        bool IsFile(const fs::path& path)
        {
            std::error_code error;
            return fs::is_regular_file(path, error);
        }

        bool IsValidExecutableName(const std::string& name)
        {
            if (name.empty() || name == "." || name == "..")
                return false;

            return std::all_of(name.begin(), name.end(), [](unsigned char c)
            {
                return std::isalnum(c) != 0 || c == '_' || c == '.' || c == '+' || c == '-';
            });
        }
    }

    std::optional<std::string> ReadProjectExecutableName(const fs::path& configPath)
    {
        if (!IsFile(configPath))
            return std::nullopt;

        try
        {
            const YAML::Node root = YAML::LoadFile(configPath.string());
            if (!root.IsMap())
                return std::nullopt;

            std::string name = root["executableName"].as<std::string>("");
            const auto first = name.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return std::nullopt;
            name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
            if (IsValidExecutableName(name))
                return name;
        }
        catch (const YAML::Exception&)
        {
        }

        return std::nullopt;
    }

    fs::path GetBuiltEditorExecutable(const fs::path& workspacePath)
    {
        return GetProjectEditorExecutable(workspacePath / "project");
    }

    fs::path GetProjectEditorExecutable(const fs::path& path)
    {
        std::error_code error;
        fs::path projectPath = fs::weakly_canonical(path, error);
        if (error)
            projectPath = path;

        std::optional<std::string> executableName;
        const fs::path workspacePath = projectPath.parent_path();
        if (projectPath.filename() == "project" && IsFile(workspacePath / "CMakeLists.txt"))
            executableName = ReadProjectExecutableName(workspacePath / "project_settings" / "project.canis");
        if (!executableName)
            executableName = ReadProjectExecutableName(projectPath / "project_settings" / "project.canis");

        // A configured custom name remains authoritative even before it is built.
        // Falling back to a stale legacy binary would bypass the required build.
        std::string filename = executableName.value_or("c-engine");
#if defined(_WIN32)
        filename += ".exe";
#endif
        return projectPath / filename;
    }
}
