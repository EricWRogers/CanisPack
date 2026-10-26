#include "ProjectBuild.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace CanisPack;

static void Check(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}
static void Write(const fs::path& path, const std::string& value)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << value;
}
static std::string Quote(const std::string& value)
{
#ifdef _WIN32
    return "\"" + value + "\"";
#else
    std::string result = "'";
    for (char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
#endif
}
static void Run(std::vector<std::string> args)
{
    std::string command;
    for (const auto& arg : args) command += Quote(arg) + " ";
    Check(std::system(command.c_str()) == 0, "Command failed: " + command);
}

int main()
{
    const auto root = fs::temp_directory_path() / ("canispack build 'fixture " +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        auto choices = BuildChoicesFromPresetList("Available configure presets:\n\n  \"debug\" - Debug\n  \"vr-release\"\n  \"web-release\" - Web Release\n");
        Check(choices.size() == 6 && choices.front().id == "Release", "Release must be the default");
        Check(FindBuildChoice(choices, "preset:vr-release").preset == "vr-release", "Preset missing");
        Check(FindBuildChoice(choices, "removed-preset").id == "Release", "Removed preset must fall back to Release");
        const BuildChoice unsafe{"preset:../../outside", "", "../../outside"};
        Check(GetBuildDirectory(root, unsafe).parent_path() == root / "build/canispack", "Preset escaped build directory");
        Write(root / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.20)
project(BuildFixture LANGUAGES CXX)
option(CANIS_ENABLE_EDITOR "Editor" ON)
set(CANIS_DESKTOP_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime" CACHE PATH "")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CANIS_DESKTOP_RUNTIME_OUTPUT_DIRECTORY}")
add_executable(custom-editor main.cpp)
add_executable(unrelated-test main.cpp)
target_compile_definitions(custom-editor PRIVATE BUILD_LABEL="${CMAKE_BUILD_TYPE}")
)");
        Write(root / "main.cpp", "#include <iostream>\n#ifndef BUILD_LABEL\n#define BUILD_LABEL \"other\"\n#endif\nint main(){std::cout << BUILD_LABEL;}\n");
        Write(root / "CMakePresets.json", R"({"version":3,"configurePresets":[
{"name":"base","hidden":true,"cacheVariables":{"CMAKE_BUILD_TYPE":"Release","PRESET_MARKER":"inherited"}},
{"name":"vr-release","inherits":"base","binaryDir":"${sourceDir}/unused","cacheVariables":{"CANIS_ENABLE_OPENXR":"ON"}},
{"name":"no-type","binaryDir":"${sourceDir}/unused"},
{"name":"multi","generator":"Ninja Multi-Config","binaryDir":"${sourceDir}/unused"},
{"name":"web","binaryDir":"${sourceDir}/unused","cacheVariables":{"CANIS_ENABLE_EDITOR":"OFF"}}
]})");
        auto buildOne = [&](const BuildChoice& choice)
        {
            auto build = GetBuildDirectory(root, choice);
            PrepareBuildQuery(build);
            Run(ConfigureArguments(root, choice));
            if (ReadBuildCache(build, "CMAKE_BUILD_TYPE").empty() && ReadBuildCache(build, "CMAKE_CONFIGURATION_TYPES").empty())
            {
                auto args = ConfigureArguments(root, choice);
                args.push_back("-DCMAKE_BUILD_TYPE=Release");
                Run(args);
            }
            std::string error, target;
            Check(CanLaunchEditorBuild(build, error), "Native editor was rejected");
            auto filename = std::string("custom-editor");
#ifdef _WIN32
            filename += ".exe";
#endif
            auto executable = FindBuildExecutable(build, filename, target);
            Check(target == "custom-editor", "Resolved an unrelated executable target");
            Run({"cmake", "--build", build.string(), "--config", BuildConfiguration(build), "--target", target, "--parallel", "2"});
            Check(fs::is_regular_file(executable), "Artifact missing after build");
            Check(executable.parent_path() == build / "runtime" || executable.parent_path() == build / "runtime" / BuildConfiguration(build), "Runtime was not isolated");
            return executable;
        };
        const auto release = buildOne(choices.front());
        Check(ReadBuildCache(GetBuildDirectory(root, choices.front()), "CMAKE_BUILD_TYPE") == "Release", "Default lacks optimization");
        const auto debug = buildOne(choices[2]);
        Check(release != debug && fs::exists(release), "Debug overwrote Release");
        const auto vr = FindBuildChoice(choices, "preset:vr-release");
        buildOne(vr);
        const auto vrBuild = GetBuildDirectory(root, vr);
        Check(ReadBuildCache(vrBuild, "PRESET_MARKER") == "inherited", "Lost preset inheritance");
        Check(ReadBuildCache(vrBuild, "CANIS_ENABLE_OPENXR") == "ON", "Lost preset feature flags");
        buildOne({"preset:no-type", "no-type", "no-type"});
        if (std::system("ninja --version") == 0)
            buildOne({"preset:multi", "multi", "multi"});
        BuildChoice web{"preset:web", "web", "web"};
        auto webBuild = GetBuildDirectory(root, web);
        PrepareBuildQuery(webBuild);
        Run(ConfigureArguments(root, web));
        std::string error;
        Check(!CanLaunchEditorBuild(webBuild, error) && !error.empty(), "Editor-disabled preset accepted");
        bool missingRejected = false;
        try { std::string target; FindBuildExecutable(vrBuild, "stale-editor", target); }
        catch (const std::exception&) { missingRejected = true; }
        Check(missingRejected, "Missing selected artifact fell back to stale executable");
        fs::remove_all(root);
        std::cout << "Build selection integration checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\nFixture: " << root << '\n';
        return 1;
    }
}
