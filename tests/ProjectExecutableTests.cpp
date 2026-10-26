#include "ProjectExecutable.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    void Write(const fs::path& path, const std::string& content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path);
        file << content;
        Check(bool(file), "Could not write fixture");
    }

    fs::path Executable(const fs::path& project, std::string name)
    {
#if defined(_WIN32)
        name += ".exe";
#endif
        return project / name;
    }

    struct Fixture
    {
        fs::path root = fs::temp_directory_path() /
            ("canispack-executables-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::path project = root / "project";
        fs::path sourceConfig = root / "project_settings/project.canis";
        fs::path runtimeConfig = project / "project_settings/project.canis";

        Fixture()
        {
            Write(root / "CMakeLists.txt", "# Workspace fixture\n");
            fs::create_directories(project);
        }

        ~Fixture()
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
    };

    void WorkspaceNames()
    {
        Fixture fixture;
        Write(fixture.sourceConfig, "executableName: matrix-mechanic\n");
        Write(fixture.runtimeConfig, "executableName: old-game\n");
        Write(Executable(fixture.project, "c-engine"), "Stale legacy binary\n");
        const auto expected = Executable(fixture.project, "matrix-mechanic");
        Check(CanisPack::GetBuiltEditorExecutable(fixture.root) == expected,
              "A stale legacy binary hid a missing configured executable");
        Check(CanisPack::GetProjectEditorExecutable(fixture.project) == expected,
              "Workspace and nested-project lookup disagreed");
        Write(expected, "New custom binary\n");
        Check(CanisPack::GetBuiltEditorExecutable(fixture.root) == expected,
              "Custom executable was not found after building");
        fs::remove(fixture.sourceConfig);
        Check(CanisPack::GetBuiltEditorExecutable(fixture.root) == Executable(fixture.project, "old-game"),
              "Runtime settings were ignored when workspace settings were missing");
        Write(fixture.sourceConfig, "gameName: Older workspace\n");
        Check(CanisPack::GetBuiltEditorExecutable(fixture.root) == Executable(fixture.project, "old-game"),
              "A missing source executableName masked the runtime name");
        Write(fixture.sourceConfig, "executableName: [malformed\n");
        Check(CanisPack::GetBuiltEditorExecutable(fixture.root) == Executable(fixture.project, "old-game"),
              "Malformed source settings masked valid runtime settings");
    }

    void StandaloneAndLegacy()
    {
        Fixture fixture;
        const auto standalone = fixture.root / "standalone";
        Write(standalone / "project_settings/project.canis", "executableName: '  My.Game+Editor-2  '\n");
        Check(CanisPack::GetProjectEditorExecutable(standalone) == Executable(standalone, "My.Game+Editor-2"),
              "Standalone custom names or supported filename characters failed");
        Check(CanisPack::GetBuiltEditorExecutable(fixture.root) == Executable(fixture.project, "c-engine"),
              "Legacy projects without an executable name lost their default");
        Write(fixture.runtimeConfig, "gameName: Legacy\n");
        Check(CanisPack::GetProjectEditorExecutable(fixture.project) == Executable(fixture.project, "c-engine"),
              "Legacy runtime settings did not default to c-engine");
        Write(fixture.runtimeConfig, "executableName: runtime-editor\n");
        // A standalone directory named project must not depend on its parent settings.
        fs::remove(fixture.root / "CMakeLists.txt");
        Write(fixture.sourceConfig, "executableName: unrelated\n");
        Check(CanisPack::GetProjectEditorExecutable(fixture.project) == Executable(fixture.project, "runtime-editor"),
              "Standalone project inherited unrelated parent settings");
    }

    void InvalidSettings()
    {
        Fixture fixture;
        for (const char* contents : {"executableName: '../escape'\n", "executableName: '/bin/false'\n",
                                    "executableName: '..'\n", "executableName: '.'\n", "executableName: ''\n",
                                    "executableName: 'bad name'\n", "executableName: [list]\n", "42\n", "null\n"})
        {
            Write(fixture.sourceConfig, contents);
            Check(!CanisPack::ReadProjectExecutableName(fixture.sourceConfig),
                  "An invalid executable setting was accepted");
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        WorkspaceNames();
        StandaloneAndLegacy();
        InvalidSettings();
        if (argc > 1)
        {
            const fs::path workspace = fs::weakly_canonical(argv[1]);
            const auto actual = CanisPack::GetBuiltEditorExecutable(workspace);
            Check(actual == Executable(workspace / "project", "matrix-mechanic") && fs::is_regular_file(actual),
                  "MatrixMechanic's actual custom executable was not resolved");
            std::cout << "MatrixMechanic executable: " << actual << '\n';
        }
        std::cout << "Project executable discovery passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
