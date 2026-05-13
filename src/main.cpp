#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_process.h>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace
{
    namespace fs = std::filesystem;

    struct CreateProjectTask
    {
        std::thread worker;
        std::mutex mutex;
        std::string step = "";
        std::string log = "";
        std::string error = "";
        fs::path workspacePath = {};
        fs::path projectPath = {};
        fs::path editorExecutable = {};
        float progress = 0.0f;
        bool running = false;
        bool completed = false;
        bool succeeded = false;
        bool launchAttempted = false;
        bool popupOpen = false;
    };

    enum class FolderDialogTarget
    {
        None,
        CreateProjectLocation,
        OpenExistingProject
    };

    struct FolderDialogState
    {
        std::mutex mutex;
        std::string defaultLocation = "";
        std::string selectedPath = "";
        std::string error = "";
        FolderDialogTarget target = FolderDialogTarget::None;
        bool active = false;
        bool pending = false;
        bool canceled = false;
    };

    struct AppState
    {
        std::vector<fs::path> recentProjects = {};
        std::string projectName = "New Canis Project";
        std::string projectLocation = "";
        std::string openProjectPath = "";
        std::string templateRepository = CANISPACK_TEMPLATE_REPOSITORY;
        std::vector<std::string> templateTags = {};
        std::string selectedTemplateTag = "";
        std::string message = "";
        bool messageIsError = false;
        bool closeAfterLaunch = false;
        CreateProjectTask createTask = {};
        FolderDialogState folderDialog = {};
    };

    struct ProjectCreateSettings
    {
        std::string projectName = "";
        std::string projectLocation = "";
        std::string templateRepository = "";
        std::string selectedTemplateTag = "";
    };

    struct CommandResult
    {
        int exitCode = -1;
        std::string output = "";
    };

    fs::path WeaklyCanonicalPath(const fs::path &_path)
    {
        std::error_code ec;
        const fs::path canonicalPath = fs::weakly_canonical(_path, ec);
        return ec ? _path.lexically_normal() : canonicalPath;
    }

    fs::path GetExecutableBasePath()
    {
        if (const char *basePath = SDL_GetBasePath())
            return fs::path(basePath);

        std::error_code ec;
        return fs::current_path(ec);
    }

    fs::path GetConfigBasePath()
    {
#if defined(_WIN32)
        if (const char *appData = std::getenv("APPDATA"))
            return fs::path(appData);
#endif

        if (const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME"))
        {
            if (xdgConfigHome[0] != '\0')
                return fs::path(xdgConfigHome);
        }

        if (const char *home = std::getenv("HOME"))
            return fs::path(home) / ".config";

        return GetExecutableBasePath() / "user_settings";
    }

    fs::path GetConfigPath()
    {
        return GetConfigBasePath() / "canispack" / "canispack.conf";
    }

    fs::path GetLegacyConfigPath()
    {
        return GetExecutableBasePath() / "user_settings" / "canispack.conf";
    }

    fs::path GetAssetPath(const fs::path &_assetPath)
    {
        const fs::path localAssetPath = GetExecutableBasePath() / "assets" / _assetPath;
        std::error_code ec;
        if (fs::exists(localAssetPath, ec) && fs::is_regular_file(localAssetPath, ec))
            return localAssetPath;

        return fs::path(CANISPACK_INSTALL_ASSET_DIR) / _assetPath;
    }

    void SetCanisPackWindowIcon(SDL_Window *_window)
    {
        const fs::path iconPath = GetAssetPath("canispack_icon.bmp");
        SDL_Surface *icon = SDL_LoadBMP(iconPath.string().c_str());
        if (icon == nullptr)
        {
            SDL_Log("Failed to load CanisPack icon '%s': %s", iconPath.string().c_str(), SDL_GetError());
            return;
        }

        SDL_SetWindowIcon(_window, icon);
        SDL_DestroySurface(icon);
    }

    void SetCanisPackDefaultFont(ImGuiIO &_io)
    {
        const fs::path fontPath = GetAssetPath("fonts/CascadiaMono/CaskaydiaMonoNerdFont-Regular.ttf");
        ImFont *font = _io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 15.0f);
        if (font == nullptr)
        {
            SDL_Log("Failed to load CanisPack font '%s'", fontPath.string().c_str());
            return;
        }

        _io.FontDefault = font;
    }

    fs::path GetDefaultProjectsDirectory()
    {
        if (const char *home = std::getenv("HOME"))
            return fs::path(home) / "Projects";

#if defined(_WIN32)
        if (const char *profile = std::getenv("USERPROFILE"))
            return fs::path(profile) / "Projects";
#endif

        std::error_code ec;
        return fs::current_path(ec) / "Projects";
    }

    bool IsDirectory(const fs::path &_path)
    {
        std::error_code ec;
        return fs::exists(_path, ec) && fs::is_directory(_path, ec);
    }

    bool IsFile(const fs::path &_path)
    {
        std::error_code ec;
        return fs::exists(_path, ec) && fs::is_regular_file(_path, ec);
    }

    bool IsProjectDirectory(const fs::path &_path)
    {
        return IsDirectory(_path / "assets") && IsFile(_path / "project_settings" / "project.canis");
    }

    std::optional<fs::path> NormalizeProjectPath(const fs::path &_path)
    {
        if (_path.empty())
            return std::nullopt;

        const fs::path normalizedPath = WeaklyCanonicalPath(_path);
        if (IsProjectDirectory(normalizedPath))
            return normalizedPath;

        const fs::path nestedProjectPath = WeaklyCanonicalPath(normalizedPath / "project");
        if (IsProjectDirectory(nestedProjectPath))
            return nestedProjectPath;

        return std::nullopt;
    }

    std::string GetProjectDisplayName(const fs::path &_projectPath)
    {
        if (_projectPath.filename() == "project" && !_projectPath.parent_path().empty())
            return _projectPath.parent_path().filename().string();

        return _projectPath.filename().string();
    }

    std::string TrimCopy(const std::string &_value)
    {
        std::size_t begin = 0u;
        while (begin < _value.size() && std::isspace(static_cast<unsigned char>(_value[begin])) != 0)
            ++begin;

        std::size_t end = _value.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(_value[end - 1u])) != 0)
            --end;

        return _value.substr(begin, end - begin);
    }

    std::string GetFolderDialogDefaultLocation(const std::string &_location)
    {
        fs::path path = TrimCopy(_location);
        if (path.empty())
            path = GetDefaultProjectsDirectory();

        while (!path.empty())
        {
            if (IsDirectory(path))
                return WeaklyCanonicalPath(path).string();

            const fs::path parentPath = path.parent_path();
            if (parentPath == path)
                break;
            path = parentPath;
        }

        std::error_code ec;
        return fs::current_path(ec).string();
    }

    std::string SanitizeProjectFolderName(const std::string &_name)
    {
        const std::string trimmed = TrimCopy(_name);
        std::string sanitized;
        sanitized.reserve(trimmed.size());

        bool previousWasSeparator = false;
        for (unsigned char c : trimmed)
        {
            if (std::isalnum(c) != 0 || c == '-' || c == '_')
            {
                sanitized.push_back(static_cast<char>(c));
                previousWasSeparator = false;
                continue;
            }

            if ((std::isspace(c) != 0 || c == '.' || c == '/') && !previousWasSeparator)
            {
                sanitized.push_back('_');
                previousWasSeparator = true;
            }
        }

        while (!sanitized.empty() && sanitized.back() == '_')
            sanitized.pop_back();

        return sanitized.empty() ? "NewCanisProject" : sanitized;
    }

    std::string ShellQuote(const std::string &_value)
    {
#if defined(_WIN32)
        std::string quoted = "\"";
        for (char c : _value)
        {
            if (c == '"' || c == '\\')
                quoted.push_back('\\');
            quoted.push_back(c);
        }
        quoted.push_back('"');
        return quoted;
#else
        std::string quoted = "'";
        for (char c : _value)
        {
            if (c == '\'')
                quoted += "'\\''";
            else
                quoted.push_back(c);
        }
        quoted.push_back('\'');
        return quoted;
#endif
    }

    CommandResult RunCommandCapture(const std::string &_command)
    {
        CommandResult result;
        const std::string command = _command + " 2>&1";

#if defined(_WIN32)
        FILE *pipe = _popen(command.c_str(), "r");
#else
        FILE *pipe = popen(command.c_str(), "r");
#endif

        if (pipe == nullptr)
        {
            result.output = "Could not start command.";
            return result;
        }

        std::array<char, 4096> buffer = {};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            result.output += buffer.data();

#if defined(_WIN32)
        result.exitCode = _pclose(pipe);
#else
        const int status = pclose(pipe);
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif

        return result;
    }

    CommandResult RunCommandStream(const std::string &_command, const std::function<void(const std::string&)> &_onOutput)
    {
        CommandResult result;
        const std::string command = _command + " 2>&1";

#if defined(_WIN32)
        FILE *pipe = _popen(command.c_str(), "r");
#else
        FILE *pipe = popen(command.c_str(), "r");
#endif

        if (pipe == nullptr)
        {
            result.output = "Could not start command.";
            if (_onOutput)
                _onOutput(result.output + "\n");
            return result;
        }

        std::array<char, 4096> buffer = {};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        {
            const std::string chunk = buffer.data();
            result.output += chunk;
            if (_onOutput)
                _onOutput(chunk);
        }

#if defined(_WIN32)
        result.exitCode = _pclose(pipe);
#else
        const int status = pclose(pipe);
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif

        return result;
    }

    std::vector<std::string> ParseTagList(const std::string &_gitOutput)
    {
        std::vector<std::string> tags;
        std::size_t lineStart = 0u;
        constexpr const char *tagPrefix = "refs/tags/";

        while (lineStart < _gitOutput.size())
        {
            std::size_t lineEnd = _gitOutput.find('\n', lineStart);
            if (lineEnd == std::string::npos)
                lineEnd = _gitOutput.size();

            const std::string line = _gitOutput.substr(lineStart, lineEnd - lineStart);
            const std::size_t refStart = line.find(tagPrefix);
            if (refStart != std::string::npos)
            {
                std::string tag = line.substr(refStart + std::char_traits<char>::length(tagPrefix));
                if (!tag.empty() && tag.ends_with("^{}"))
                    tag.resize(tag.size() - 3u);
                if (!tag.empty())
                    tags.push_back(tag);
            }

            lineStart = lineEnd + 1u;
        }

        std::sort(tags.begin(), tags.end());
        tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
        return tags;
    }

    void SetMessage(AppState &_state, std::string _message, bool _isError)
    {
        _state.message = std::move(_message);
        _state.messageIsError = _isError;
    }

    void SDLCALL OnFolderDialogResult(void *_userdata, const char * const *_filelist, int)
    {
        FolderDialogState *dialog = static_cast<FolderDialogState*>(_userdata);
        if (dialog == nullptr)
            return;

        std::lock_guard<std::mutex> lock(dialog->mutex);
        dialog->selectedPath.clear();
        dialog->error.clear();
        dialog->canceled = false;
        dialog->pending = true;
        dialog->active = false;

        if (_filelist == nullptr)
        {
            dialog->error = SDL_GetError();
            if (dialog->error.empty())
                dialog->error = "Could not open folder dialog.";
            return;
        }

        if (_filelist[0] == nullptr)
        {
            dialog->canceled = true;
            return;
        }

        dialog->selectedPath = _filelist[0];
    }

    void ShowFolderDialog(
        AppState &_state,
        SDL_Window *_window,
        FolderDialogTarget _target,
        const std::string &_defaultLocation)
    {
        {
            std::lock_guard<std::mutex> lock(_state.folderDialog.mutex);
            if (_state.folderDialog.active)
            {
                SetMessage(_state, "A folder picker is already open.", true);
                return;
            }

            _state.folderDialog.defaultLocation = _defaultLocation;
            _state.folderDialog.selectedPath.clear();
            _state.folderDialog.error.clear();
            _state.folderDialog.target = _target;
            _state.folderDialog.pending = false;
            _state.folderDialog.canceled = false;
            _state.folderDialog.active = true;
        }

        const char *defaultLocation = _state.folderDialog.defaultLocation.empty() ? nullptr : _state.folderDialog.defaultLocation.c_str();
        SDL_ShowOpenFolderDialog(OnFolderDialogResult, &_state.folderDialog, _window, defaultLocation, false);
    }

    void AddRecentProject(AppState &_state, const fs::path &_projectPath)
    {
        const fs::path normalizedPath = WeaklyCanonicalPath(_projectPath);
        _state.recentProjects.erase(
            std::remove_if(_state.recentProjects.begin(), _state.recentProjects.end(), [&](const fs::path &_path)
            {
                return WeaklyCanonicalPath(_path) == normalizedPath;
            }),
            _state.recentProjects.end());

        _state.recentProjects.insert(_state.recentProjects.begin(), normalizedPath);

        constexpr std::size_t maxRecentProjects = 12u;
        if (_state.recentProjects.size() > maxRecentProjects)
            _state.recentProjects.resize(maxRecentProjects);
    }

    bool RefreshTemplateTags(AppState &_state, std::string &_outError)
    {
        const std::string repository = TrimCopy(_state.templateRepository);
        if (repository.empty())
        {
            _outError = "Template repository is empty.";
            return false;
        }

        const CommandResult result = RunCommandCapture("git ls-remote --tags --refs " + ShellQuote(repository));
        if (result.exitCode != 0)
        {
            _outError = "Could not fetch template releases.";
            if (!result.output.empty())
                _outError += "\n" + result.output;
            return false;
        }

        std::vector<std::string> tags = ParseTagList(result.output);
        if (tags.empty())
        {
            _outError = "Template repository has no release tags.";
            return false;
        }

        const bool keepSelection = !TrimCopy(_state.selectedTemplateTag).empty() &&
            std::find(tags.begin(), tags.end(), _state.selectedTemplateTag) != tags.end();

        _state.templateTags = std::move(tags);
        if (!keepSelection)
            _state.selectedTemplateTag = _state.templateTags.back();

        return true;
    }

    void LoadConfig(AppState &_state)
    {
        fs::path configPath = GetConfigPath();
        if (!IsFile(configPath))
        {
            const fs::path legacyConfigPath = GetLegacyConfigPath();
            if (!IsFile(legacyConfigPath))
                return;

            configPath = legacyConfigPath;
        }

        try
        {
            const YAML::Node root = YAML::LoadFile(configPath.string());
            _state.templateRepository = root["templateRepository"].as<std::string>(_state.templateRepository);
            _state.selectedTemplateTag = root["templateRelease"].as<std::string>(_state.selectedTemplateTag);
            _state.closeAfterLaunch = root["closeAfterLaunch"].as<bool>(_state.closeAfterLaunch);

            if (const YAML::Node recent = root["recentProjects"])
            {
                for (const YAML::Node &entry : recent)
                {
                    if (std::optional<fs::path> projectPath = NormalizeProjectPath(entry.as<std::string>("")))
                        AddRecentProject(_state, *projectPath);
                }
            }
        }
        catch (const YAML::Exception &)
        {
        }
    }

    void SaveConfig(const AppState &_state)
    {
        YAML::Node root;
        root["templateRepository"] = _state.templateRepository;
        root["templateRelease"] = _state.selectedTemplateTag;
        root["closeAfterLaunch"] = _state.closeAfterLaunch;

        YAML::Node recent(YAML::NodeType::Sequence);
        for (const fs::path &projectPath : _state.recentProjects)
            recent.push_back(projectPath.generic_string());
        root["recentProjects"] = recent;

        const fs::path configPath = GetConfigPath();
        std::error_code ec;
        fs::create_directories(configPath.parent_path(), ec);
        if (ec)
            return;

        std::ofstream out(configPath);
        if (out.is_open())
            out << root;
    }

    void SetCreateTaskStep(CreateProjectTask &_task, std::string _step, float _progress)
    {
        std::lock_guard<std::mutex> lock(_task.mutex);
        _task.step = std::move(_step);
        _task.progress = _progress;
    }

    void AppendCreateTaskLog(CreateProjectTask &_task, const std::string &_chunk)
    {
        std::lock_guard<std::mutex> lock(_task.mutex);
        _task.log += _chunk;

        constexpr std::size_t maxLogSize = 80000u;
        if (_task.log.size() > maxLogSize)
            _task.log.erase(0u, _task.log.size() - maxLogSize);
    }

    void CompleteCreateTask(
        CreateProjectTask &_task,
        bool _succeeded,
        std::string _error,
        fs::path _workspacePath,
        fs::path _projectPath,
        fs::path _editorExecutable)
    {
        std::lock_guard<std::mutex> lock(_task.mutex);
        _task.running = false;
        _task.completed = true;
        _task.succeeded = _succeeded;
        _task.error = std::move(_error);
        _task.workspacePath = std::move(_workspacePath);
        _task.projectPath = std::move(_projectPath);
        _task.editorExecutable = std::move(_editorExecutable);
        _task.progress = _succeeded ? 1.0f : 0.0f;
        _task.step = _succeeded ? "Build finished. Launching editor..." : "Create project failed.";
    }

    CommandResult RunCreateTaskCommand(CreateProjectTask &_task, const std::string &_command)
    {
        AppendCreateTaskLog(_task, "\n> " + _command + "\n");
        return RunCommandStream(_command, [&](const std::string &_chunk)
        {
            AppendCreateTaskLog(_task, _chunk);
        });
    }

    fs::path GetBuiltEditorExecutable(const fs::path &_workspacePath)
    {
#if defined(_WIN32)
        return _workspacePath / "project" / "c-engine.exe";
#else
        return _workspacePath / "project" / "c-engine";
#endif
    }

    fs::path GetProjectEditorExecutable(const fs::path &_projectPath)
    {
        const fs::path projectPath = WeaklyCanonicalPath(_projectPath);
        if (projectPath.filename() == "project" && !projectPath.parent_path().empty())
            return GetBuiltEditorExecutable(projectPath.parent_path());

#if defined(_WIN32)
        return projectPath / "c-engine.exe";
#else
        return projectPath / "c-engine";
#endif
    }

    bool CloneTemplateProject(
        const ProjectCreateSettings &_settings,
        const fs::path &_projectPath,
        CreateProjectTask &_task,
        std::string &_outError)
    {
        const std::string repository = TrimCopy(_settings.templateRepository);
        const std::string release = TrimCopy(_settings.selectedTemplateTag);
        if (repository.empty())
        {
            _outError = "Template repository is empty.";
            return false;
        }

        if (release.empty())
        {
            _outError = "Choose a template release first.";
            return false;
        }

        std::error_code ec;
        fs::create_directories(_projectPath.parent_path(), ec);
        if (ec)
        {
            _outError = "Could not create project parent folder.";
            return false;
        }

        const std::string command =
            "git clone --depth 1 --branch " + ShellQuote(release) +
            " --single-branch --recurse-submodules --shallow-submodules -- " +
            ShellQuote(repository) + " " + ShellQuote(_projectPath.string());

        const CommandResult result = RunCreateTaskCommand(_task, command);
        if (result.exitCode != 0)
        {
            _outError = "Could not clone template release.";
            if (!result.output.empty())
                _outError += "\n" + result.output;
            return false;
        }

        const std::string gitDirectoryArgument = "-C " + ShellQuote(_projectPath.string());
        const CommandResult branchResult = RunCreateTaskCommand(_task, "git " + gitDirectoryArgument + " checkout -B main");
        if (branchResult.exitCode != 0)
        {
            _outError = "Template cloned, but the new project branch could not be prepared.";
            if (!branchResult.output.empty())
                _outError += "\n" + branchResult.output;
            return false;
        }

        const CommandResult remoteResult = RunCreateTaskCommand(_task, "git " + gitDirectoryArgument + " remote remove origin");
        if (remoteResult.exitCode != 0)
        {
            _outError = "Template cloned, but its template remote could not be removed.";
            if (!remoteResult.output.empty())
                _outError += "\n" + remoteResult.output;
            return false;
        }

        return true;
    }

    bool CreateProject(
        const ProjectCreateSettings &_settings,
        CreateProjectTask &_task,
        fs::path &_outWorkspacePath,
        fs::path &_outProjectPath,
        std::string &_outError)
    {
        const fs::path projectPath = WeaklyCanonicalPath(fs::path(_settings.projectLocation)) / SanitizeProjectFolderName(_settings.projectName);
        std::error_code ec;
        if (fs::exists(projectPath, ec) && !fs::is_directory(projectPath, ec))
        {
            _outError = "A file already exists at that path.";
            return false;
        }

        if (fs::exists(projectPath, ec) && !fs::is_empty(projectPath, ec))
        {
            _outError = "Project folder already exists and is not empty.";
            return false;
        }

        SetCreateTaskStep(_task, "Cloning template release...", 0.25f);
        if (!CloneTemplateProject(_settings, projectPath, _task, _outError))
        {
            fs::remove_all(projectPath, ec);
            return false;
        }

        const std::optional<fs::path> normalizedProjectPath = NormalizeProjectPath(projectPath);
        if (!normalizedProjectPath.has_value())
        {
            _outError = "Template release did not contain a Canis project or project/ folder.";
            fs::remove_all(projectPath, ec);
            return false;
        }

        _outWorkspacePath = WeaklyCanonicalPath(projectPath);
        _outProjectPath = *normalizedProjectPath;
        return true;
    }

    bool LaunchProjectWithExecutable(const fs::path &_executablePath, const fs::path &_projectPath, std::string &_outError)
    {
        const fs::path executablePath = WeaklyCanonicalPath(_executablePath);
        if (!IsFile(executablePath))
        {
            _outError = "Engine executable not found: " + executablePath.generic_string();
            return false;
        }

        const fs::path projectPath = WeaklyCanonicalPath(_projectPath);
        if (!IsProjectDirectory(projectPath))
        {
            _outError = "Selected folder is not a Canis project.";
            return false;
        }

        std::vector<std::string> argsStorage = { executablePath.string() };
        std::vector<const char*> args = {};
        for (const std::string &arg : argsStorage)
            args.push_back(arg.c_str());
        args.push_back(nullptr);

        SDL_Environment *environment = SDL_CreateEnvironment(true);
        if (environment == nullptr)
        {
            _outError = SDL_GetError();
            return false;
        }

        if (!SDL_SetEnvironmentVariable(environment, "CANIS_PROJECT", projectPath.string().c_str(), true))
        {
            _outError = SDL_GetError();
            SDL_DestroyEnvironment(environment);
            return false;
        }

        SDL_PropertiesID properties = SDL_CreateProperties();
        if (properties == 0)
        {
            _outError = SDL_GetError();
            SDL_DestroyEnvironment(environment);
            return false;
        }

        bool success = SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<const char**>(args.data()));
        success = success && SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
        success = success && SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, executablePath.parent_path().string().c_str());
        success = success && SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
        success = success && SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_INHERITED);
        success = success && SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
        if (!success)
        {
            _outError = SDL_GetError();
            SDL_DestroyProperties(properties);
            SDL_DestroyEnvironment(environment);
            return false;
        }

        SDL_Process *process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        SDL_DestroyEnvironment(environment);

        if (process == nullptr)
        {
            _outError = SDL_GetError();
            return false;
        }

        SDL_DestroyProcess(process);
        return true;
    }

    bool LaunchProject(const fs::path &_projectPath, std::string &_outError)
    {
        const fs::path executablePath = GetProjectEditorExecutable(_projectPath);
        if (!IsFile(executablePath))
        {
            _outError = "Project editor executable not found: " + executablePath.generic_string();
            return false;
        }

        return LaunchProjectWithExecutable(executablePath, _projectPath, _outError);
    }

    bool BuildProjectWorkspace(const fs::path &_workspacePath, CreateProjectTask &_task, std::string &_outError)
    {
        const fs::path buildPath = _workspacePath / "build";

        SetCreateTaskStep(_task, "Configuring project build...", 0.50f);
        const std::string configureCommand =
            "cmake -S " + ShellQuote(_workspacePath.string()) + " -B " + ShellQuote(buildPath.string());

        const CommandResult configureResult = RunCreateTaskCommand(_task, configureCommand);
        if (configureResult.exitCode != 0)
        {
            _outError = "Could not configure the new project build.";
            if (!configureResult.output.empty())
                _outError += "\n" + configureResult.output;
            return false;
        }

        SetCreateTaskStep(_task, "Building editor...", 0.75f);
        const std::string buildCommand = "cmake --build " + ShellQuote(buildPath.string()) + " --parallel";
        const CommandResult buildResult = RunCreateTaskCommand(_task, buildCommand);
        if (buildResult.exitCode != 0)
        {
            _outError = "Could not build the new project.";
            if (!buildResult.output.empty())
                _outError += "\n" + buildResult.output;
            return false;
        }

        const fs::path editorExecutable = GetBuiltEditorExecutable(_workspacePath);
        if (!IsFile(editorExecutable))
        {
            _outError = "Build finished, but the editor executable was not found: " + editorExecutable.generic_string();
            return false;
        }

        return true;
    }

    bool IsCreateTaskRunning(CreateProjectTask &_task)
    {
        std::lock_guard<std::mutex> lock(_task.mutex);
        return _task.running;
    }

    bool IsCreateTaskCompleted(CreateProjectTask &_task)
    {
        std::lock_guard<std::mutex> lock(_task.mutex);
        return _task.completed;
    }

    void JoinCompletedCreateTask(CreateProjectTask &_task)
    {
        if (_task.worker.joinable() && IsCreateTaskCompleted(_task))
            _task.worker.join();
    }

    void StartCreateProjectTask(AppState &_state)
    {
        JoinCompletedCreateTask(_state.createTask);

        if (_state.createTask.worker.joinable())
        {
            SetMessage(_state, "A project is already being created.", true);
            return;
        }

        const ProjectCreateSettings settings =
        {
            _state.projectName,
            _state.projectLocation,
            _state.templateRepository,
            _state.selectedTemplateTag
        };

        {
            std::lock_guard<std::mutex> lock(_state.createTask.mutex);
            _state.createTask.step = "Preparing project...";
            _state.createTask.log.clear();
            _state.createTask.error.clear();
            _state.createTask.workspacePath.clear();
            _state.createTask.projectPath.clear();
            _state.createTask.editorExecutable.clear();
            _state.createTask.progress = 0.05f;
            _state.createTask.running = true;
            _state.createTask.completed = false;
            _state.createTask.succeeded = false;
            _state.createTask.launchAttempted = false;
            _state.createTask.popupOpen = true;
        }

        CreateProjectTask *task = &_state.createTask;
        task->worker = std::thread([settings, task]()
        {
            fs::path workspacePath;
            fs::path projectPath;
            std::string error;

            if (!CreateProject(settings, *task, workspacePath, projectPath, error))
            {
                CompleteCreateTask(*task, false, error.empty() ? "Could not create project." : error, workspacePath, projectPath, {});
                return;
            }

            if (!BuildProjectWorkspace(workspacePath, *task, error))
            {
                CompleteCreateTask(*task, false, error.empty() ? "Could not build project." : error, workspacePath, projectPath, {});
                return;
            }

            CompleteCreateTask(*task, true, "", workspacePath, projectPath, GetBuiltEditorExecutable(workspacePath));
        });
    }

    void ProcessCreateTaskResult(AppState &_state, bool &_running)
    {
        fs::path projectPath;
        fs::path editorExecutable;
        bool shouldLaunch = false;

        {
            std::lock_guard<std::mutex> lock(_state.createTask.mutex);
            shouldLaunch = _state.createTask.completed && _state.createTask.succeeded && !_state.createTask.launchAttempted;
            if (shouldLaunch)
            {
                _state.createTask.launchAttempted = true;
                projectPath = _state.createTask.projectPath;
                editorExecutable = _state.createTask.editorExecutable;
            }
        }

        if (!shouldLaunch)
            return;

        JoinCompletedCreateTask(_state.createTask);

        std::string error;
        if (!LaunchProjectWithExecutable(editorExecutable, projectPath, error))
        {
            {
                std::lock_guard<std::mutex> lock(_state.createTask.mutex);
                _state.createTask.succeeded = false;
                _state.createTask.error = error.empty() ? "Launch failed." : error;
                _state.createTask.step = "Launch failed.";
                _state.createTask.popupOpen = true;
            }

            SetMessage(_state, error.empty() ? "Launch failed." : error, true);
            return;
        }

        AddRecentProject(_state, projectPath);
        SaveConfig(_state);
        SetMessage(_state, "Created and launched " + GetProjectDisplayName(projectPath), false);

        {
            std::lock_guard<std::mutex> lock(_state.createTask.mutex);
            _state.createTask.step = "Editor launched.";
            _state.createTask.popupOpen = false;
        }

        if (_state.closeAfterLaunch)
            _running = false;
    }

    void DrawCreateProjectPopup(AppState &_state)
    {
        std::string step;
        std::string log;
        std::string error;
        float progress = 0.0f;
        bool running = false;
        bool completed = false;
        bool succeeded = false;
        bool launchAttempted = false;
        bool popupOpen = false;

        {
            std::lock_guard<std::mutex> lock(_state.createTask.mutex);
            step = _state.createTask.step;
            log = _state.createTask.log;
            error = _state.createTask.error;
            progress = _state.createTask.progress;
            running = _state.createTask.running;
            completed = _state.createTask.completed;
            succeeded = _state.createTask.succeeded;
            launchAttempted = _state.createTask.launchAttempted;
            popupOpen = _state.createTask.popupOpen;
        }

        const bool shouldOpen = popupOpen || running || (completed && succeeded && !launchAttempted);
        if (shouldOpen)
            ImGui::OpenPopup("Create Project");

        if (!ImGui::BeginPopupModal("Create Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        if (!shouldOpen && completed && succeeded && launchAttempted)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextUnformatted(step.empty() ? "Preparing project..." : step.c_str());
        ImGui::ProgressBar(std::clamp(progress, 0.0f, 1.0f), ImVec2(560.0f, 0.0f));

        ImGui::Spacing();
        ImGui::BeginChild("CreateProjectLog", ImVec2(620.0f, 260.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        if (log.empty())
            ImGui::TextDisabled("Waiting for command output...");
        else
            ImGui::TextUnformatted(log.c_str());
        if (running)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        if (!error.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.30f, 1.0f), "%s", error.c_str());
        }

        ImGui::Spacing();
        if (running)
        {
            ImGui::TextDisabled("Working...");
        }
        else if (!succeeded)
        {
            if (ImGui::Button("Close", ImVec2(110.0f, 30.0f)))
            {
                {
                    std::lock_guard<std::mutex> lock(_state.createTask.mutex);
                    _state.createTask.popupOpen = false;
                }

                JoinCompletedCreateTask(_state.createTask);
                ImGui::CloseCurrentPopup();
            }
        }
        else
        {
            ImGui::TextDisabled("Launching editor...");
        }

        ImGui::EndPopup();
    }

    void OpenProject(AppState &_state, const fs::path &_path, bool &_running)
    {
        std::optional<fs::path> projectPath = NormalizeProjectPath(_path);
        if (!projectPath.has_value())
        {
            SetMessage(_state, "That folder is not a Canis project.", true);
            return;
        }

        std::string error;
        if (!LaunchProject(*projectPath, error))
        {
            SetMessage(_state, error.empty() ? "Launch failed." : error, true);
            return;
        }

        AddRecentProject(_state, *projectPath);
        SaveConfig(_state);
        SetMessage(_state, "Launched " + GetProjectDisplayName(*projectPath), false);
        if (_state.closeAfterLaunch)
            _running = false;
    }

    void ProcessFolderDialogResult(AppState &_state, bool &_running)
    {
        std::string selectedPath;
        std::string error;
        FolderDialogTarget target = FolderDialogTarget::None;
        bool pending = false;
        bool canceled = false;

        {
            std::lock_guard<std::mutex> lock(_state.folderDialog.mutex);
            pending = _state.folderDialog.pending;
            if (pending)
            {
                selectedPath = _state.folderDialog.selectedPath;
                error = _state.folderDialog.error;
                target = _state.folderDialog.target;
                canceled = _state.folderDialog.canceled;
                _state.folderDialog.pending = false;
                _state.folderDialog.target = FolderDialogTarget::None;
            }
        }

        if (!pending || canceled)
            return;

        if (!error.empty())
        {
            SetMessage(_state, error, true);
            return;
        }

        if (selectedPath.empty())
            return;

        const fs::path folderPath = WeaklyCanonicalPath(fs::path(selectedPath));
        switch (target)
        {
            case FolderDialogTarget::CreateProjectLocation:
                _state.projectLocation = folderPath.generic_string();
                SetMessage(_state, "Project location selected.", false);
                break;
            case FolderDialogTarget::OpenExistingProject:
                _state.openProjectPath = folderPath.generic_string();
                OpenProject(_state, folderPath, _running);
                break;
            case FolderDialogTarget::None:
                break;
        }
    }

    void DrawCanisPack(AppState &_state, bool &_running, SDL_Window *_window)
    {
        ProcessCreateTaskResult(_state, _running);
        ProcessFolderDialogResult(_state, _running);

        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("CanisPack", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings);

        ImGui::SetCursorPos(ImVec2(28.0f, 24.0f));
        ImGui::TextUnformatted("CanisPack");
        ImGui::SetCursorPosX(28.0f);
        ImGui::TextDisabled("Version %s", CANISPACK_VERSION);
        ImGui::Separator();

        const float sideWidth = std::min(380.0f, std::max(300.0f, io.DisplaySize.x * 0.34f));
        ImGui::BeginChild("Projects", ImVec2(sideWidth, 0.0f), true);
        ImGui::TextUnformatted("Projects");
        ImGui::Spacing();
        for (const fs::path &projectPath : _state.recentProjects)
        {
            if (!IsProjectDirectory(projectPath))
                continue;

            const std::string label = GetProjectDisplayName(projectPath) + "##" + projectPath.generic_string();
            if (ImGui::Selectable(label.c_str(), false))
                OpenProject(_state, projectPath, _running);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", projectPath.generic_string().c_str());
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("Actions", ImVec2(0.0f, 0.0f), true);
        ImGui::TextUnformatted("Create Project");
        ImGui::Spacing();
        ImGui::InputText("Name", &_state.projectName);

        ImGui::TextUnformatted("Location");
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 104.0f));
        ImGui::InputText("##ProjectLocation", &_state.projectLocation);
        ImGui::SameLine();
        if (ImGui::Button("Browse##ProjectLocation", ImVec2(96.0f, 0.0f)))
        {
            const std::string defaultLocation = _state.projectLocation.empty() ?
                GetDefaultProjectsDirectory().generic_string() :
                _state.projectLocation;
            ShowFolderDialog(_state, _window, FolderDialogTarget::CreateProjectLocation, GetFolderDialogDefaultLocation(defaultLocation));
        }

        const std::string projectFolder = (fs::path(_state.projectLocation) / SanitizeProjectFolderName(_state.projectName)).generic_string();
        ImGui::TextDisabled("%s", projectFolder.c_str());

        ImGui::Spacing();
        ImGui::TextUnformatted("Template");
        if (ImGui::InputText("Repository", &_state.templateRepository))
        {
            _state.templateTags.clear();
            _state.selectedTemplateTag.clear();
            SaveConfig(_state);
        }

        const std::string releasePreview = _state.selectedTemplateTag.empty() ? "No release selected" : _state.selectedTemplateTag;
        if (ImGui::BeginCombo("Release", releasePreview.c_str()))
        {
            if (_state.templateTags.empty())
            {
                ImGui::TextDisabled("No releases loaded");
            }
            else
            {
                for (auto tag = _state.templateTags.rbegin(); tag != _state.templateTags.rend(); ++tag)
                {
                    const bool selected = (*tag == _state.selectedTemplateTag);
                    if (ImGui::Selectable(tag->c_str(), selected))
                    {
                        _state.selectedTemplateTag = *tag;
                        SaveConfig(_state);
                    }

                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Refresh Releases", ImVec2(160.0f, 30.0f)))
        {
            std::string error;
            if (RefreshTemplateTags(_state, error))
            {
                SaveConfig(_state);
                SetMessage(_state, "Loaded " + std::to_string(_state.templateTags.size()) + " template release(s).", false);
            }
            else
            {
                SetMessage(_state, error.empty() ? "Could not load template releases." : error, true);
            }
        }

        ImGui::Spacing();
        const bool createBusy = IsCreateTaskRunning(_state.createTask);
        if (createBusy)
            ImGui::BeginDisabled();

        if (ImGui::Button("Create and Open", ImVec2(160.0f, 34.0f)))
        {
            StartCreateProjectTask(_state);
        }

        if (createBusy)
            ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Open Existing");
        ImGui::TextUnformatted("Project Path");
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 144.0f));
        ImGui::InputText("##OpenProjectPath", &_state.openProjectPath);
        ImGui::SameLine();
        if (ImGui::Button("Browse and Open##OpenProjectPath", ImVec2(136.0f, 0.0f)))
        {
            const std::string defaultLocation = !_state.openProjectPath.empty() ?
                _state.openProjectPath :
                (_state.projectLocation.empty() ? GetDefaultProjectsDirectory().generic_string() : _state.projectLocation);
            ShowFolderDialog(_state, _window, FolderDialogTarget::OpenExistingProject, GetFolderDialogDefaultLocation(defaultLocation));
        }

        if (ImGui::Button("Open Project", ImVec2(140.0f, 34.0f)))
            OpenProject(_state, _state.openProjectPath, _running);

        ImGui::Spacing();
        if (ImGui::Checkbox("Close CanisPack after launch", &_state.closeAfterLaunch))
            SaveConfig(_state);

        if (!_state.message.empty())
        {
            ImGui::Spacing();
            const ImVec4 color = _state.messageIsError ? ImVec4(1.0f, 0.36f, 0.30f, 1.0f) : ImVec4(0.40f, 0.82f, 0.50f, 1.0f);
            ImGui::TextColored(color, "%s", _state.message.c_str());
        }

        ImGui::EndChild();
        ImGui::End();

        DrawCreateProjectPopup(_state);
    }
}

int main(int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *window = SDL_CreateWindow("CanisPack", 960, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SetCanisPackWindowIcon(window);

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr)
    {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    SetCanisPackDefaultFont(io);

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    AppState state;
    state.projectLocation = GetDefaultProjectsDirectory().generic_string();
    LoadConfig(state);
    if (const char *templateRepository = std::getenv("CANIS_TEMPLATE_REPOSITORY"))
        state.templateRepository = templateRepository;
    {
        std::string error;
        if (!RefreshTemplateTags(state, error))
            SetMessage(state, error.empty() ? "Could not load template releases." : error, true);
    }

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        DrawCanisPack(state, running, window);

        ImGui::Render();

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
        glViewport(0, 0, drawableWidth, drawableHeight);
        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    if (state.createTask.worker.joinable())
        state.createTask.worker.join();

    SaveConfig(state);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
