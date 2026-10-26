#include "ProjectBuild.hpp"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace CanisPack
{
    namespace fs = std::filesystem;

    std::vector<BuildChoice> BuildChoicesFromPresetList(const std::string& output)
    {
        std::vector<BuildChoice> choices = {
            {"Release", "Release", ""},
            {"RelWithDebInfo", "Release with debug symbols", ""},
            {"Debug", "Debug", ""}
        };
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line))
        {
            const auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] != '"') continue;
            const auto end = line.find('"', start + 1);
            if (end == std::string::npos || end == start + 1) continue;
            const std::string name = line.substr(start + 1, end - start - 1);
            if (std::any_of(choices.begin(), choices.end(), [&](const auto& c) { return c.preset == name; })) continue;
            choices.push_back({"preset:" + name, name + " (preset)", name});
        }
        return choices;
    }

    BuildChoice FindBuildChoice(const std::vector<BuildChoice>& choices, const std::string& id)
    {
        for (const auto& choice : choices)
            if (choice.id == id) return choice;
        return choices.front();
    }

    fs::path GetBuildDirectory(const fs::path& workspace, const BuildChoice& choice)
    {
        std::ostringstream key;
        key << (choice.preset.empty() ? "config-" : "preset-");
        for (unsigned char c : choice.preset.empty() ? choice.id : choice.preset)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-') key << c;
            else key << '_' << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
        return workspace / "build" / "canispack" / key.str();
    }

    void PrepareBuildQuery(const fs::path& build)
    {
        const auto query = build / ".cmake/api/v1/query";
        fs::create_directories(query);
        std::ofstream file(query / "codemodel-v2");
        if (!file) throw std::runtime_error("Could not write CMake artifact query");
    }

    std::vector<std::string> ConfigureArguments(const fs::path& workspace, const BuildChoice& choice)
    {
        const auto build = GetBuildDirectory(workspace, choice);
        std::vector<std::string> args = {"cmake"};
        if (!choice.preset.empty()) { args.push_back("--preset"); args.push_back(choice.preset); }
        args.insert(args.end(), {"-S", workspace.string(), "-B", build.string(),
            "-DCANIS_DESKTOP_RUNTIME_OUTPUT_DIRECTORY=" + (build / "runtime").string(),
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + (build / "runtime").string()});
        if (choice.preset.empty())
            args.insert(args.end(), {"-DCMAKE_BUILD_TYPE=" + choice.id, "-DCANIS_ENABLE_EDITOR=ON", "-DBUILD_TESTING=OFF"});
        return args;
    }

    std::string ReadBuildCache(const fs::path& build, const std::string& key)
    {
        std::ifstream cache(build / "CMakeCache.txt");
        std::string line;
        while (std::getline(cache, line))
        {
            if (line.rfind(key + ":", 0) != 0) continue;
            const auto equals = line.find('=');
            if (equals != std::string::npos) return line.substr(equals + 1);
        }
        return "";
    }

    std::string BuildConfiguration(const fs::path& build)
    {
        const auto type = ReadBuildCache(build, "CMAKE_BUILD_TYPE");
        return type.empty() ? "Release" : type;
    }

    bool CanLaunchEditorBuild(const fs::path& build, std::string& error)
    {
        auto editor = ReadBuildCache(build, "CANIS_ENABLE_EDITOR");
        std::transform(editor.begin(), editor.end(), editor.begin(), [](unsigned char c) { return std::toupper(c); });
        if (editor == "OFF" || editor == "FALSE" || editor == "0" || editor == "NO" ||
            ReadBuildCache(build, "CANIS_PLATFORM_WEB") == "ON")
        {
            error = "This preset disables the desktop editor. Choose Release or a native editor preset to open the project.";
            return false;
        }
        return true;
    }

    fs::path FindBuildExecutable(const fs::path& build, const std::string& filename, std::string& target)
    {
        const auto reply = build / ".cmake/api/v1/reply";
        std::string latest;
        for (const auto& entry : fs::directory_iterator(reply))
        {
            auto name = entry.path().filename().string();
            if (name.starts_with("index-") && name.ends_with(".json") && name > latest) latest = name;
        }
        if (latest.empty()) throw std::runtime_error("CMake did not return build artifacts");
        const auto index = YAML::LoadFile((reply / latest).string());
        const auto modelFile = index["reply"]["codemodel-v2"]["jsonFile"].as<std::string>();
        const auto model = YAML::LoadFile((reply / modelFile).string());
        const auto configuration = BuildConfiguration(build);
        for (const auto& config : model["configurations"])
        {
            const auto name = config["name"].as<std::string>("");
            if (!name.empty() && name != configuration) continue;
            for (const auto& reference : config["targets"])
            {
                const auto item = YAML::LoadFile((reply / reference["jsonFile"].as<std::string>()).string());
                if (item["type"].as<std::string>("") != "EXECUTABLE") continue;
                for (const auto& artifact : item["artifacts"])
                {
                    auto path = fs::path(artifact["path"].as<std::string>());
                    if (path.filename() != filename) continue;
                    target = item["name"].as<std::string>();
                    return (path.is_absolute() ? path : build / path).lexically_normal();
                }
            }
        }
        throw std::runtime_error("Selected build does not produce the configured editor executable: " + filename);
    }
}
