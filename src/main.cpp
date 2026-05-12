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
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace
{
    namespace fs = std::filesystem;

    struct AppState
    {
        std::vector<fs::path> recentProjects = {};
        std::string projectName = "New Canis Project";
        std::string projectLocation = "";
        std::string openProjectPath = "";
        std::string templateRepository = CANISPACK_TEMPLATE_REPOSITORY;
        std::vector<std::string> templateTags = {};
        std::string selectedTemplateTag = "";
        std::string engineExecutable = CANISPACK_ENGINE_EXECUTABLE;
        std::string message = "";
        bool messageIsError = false;
        bool closeAfterLaunch = false;
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

    fs::path GetConfigPath()
    {
        return GetExecutableBasePath() / "user_settings" / "canispack.conf";
    }

    fs::path GetDefaultProjectsDirectory()
    {
        if (const char *home = std::getenv("HOME"))
            return fs::path(home) / "CanisProjects";

#if defined(_WIN32)
        if (const char *profile = std::getenv("USERPROFILE"))
            return fs::path(profile) / "CanisProjects";
#endif

        std::error_code ec;
        return fs::current_path(ec) / "CanisProjects";
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
        const fs::path configPath = GetConfigPath();
        if (!IsFile(configPath))
            return;

        try
        {
            const YAML::Node root = YAML::LoadFile(configPath.string());
            _state.templateRepository = root["templateRepository"].as<std::string>(_state.templateRepository);
            _state.selectedTemplateTag = root["templateRelease"].as<std::string>(_state.selectedTemplateTag);
            _state.engineExecutable = root["engineExecutable"].as<std::string>(_state.engineExecutable);
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
        root["engineExecutable"] = _state.engineExecutable;
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

    bool CloneTemplateProject(const AppState &_state, const fs::path &_projectPath, std::string &_outError)
    {
        const std::string repository = TrimCopy(_state.templateRepository);
        const std::string release = TrimCopy(_state.selectedTemplateTag);
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
            " --single-branch -- " + ShellQuote(repository) + " " + ShellQuote(_projectPath.string());

        const CommandResult result = RunCommandCapture(command);
        if (result.exitCode != 0)
        {
            _outError = "Could not clone template release.";
            if (!result.output.empty())
                _outError += "\n" + result.output;
            return false;
        }

        fs::remove_all(_projectPath / ".git", ec);
        if (ec)
        {
            _outError = "Template cloned, but its git metadata could not be removed.";
            return false;
        }

        return true;
    }

    bool CreateProject(const AppState &_state, fs::path &_outProjectPath, std::string &_outError)
    {
        const fs::path projectPath = WeaklyCanonicalPath(fs::path(_state.projectLocation)) / SanitizeProjectFolderName(_state.projectName);
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

        if (!CloneTemplateProject(_state, projectPath, _outError))
        {
            fs::remove_all(projectPath, ec);
            return false;
        }

        if (!IsProjectDirectory(projectPath))
        {
            _outError = "Template release did not contain a Canis project.";
            fs::remove_all(projectPath, ec);
            return false;
        }

        _outProjectPath = WeaklyCanonicalPath(projectPath);
        return true;
    }

    bool LaunchProject(const AppState &_state, const fs::path &_projectPath, std::string &_outError)
    {
        const fs::path executablePath = WeaklyCanonicalPath(_state.engineExecutable);
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

        bool success = SDL_SetEnvironmentVariable(environment, "CANIS_PROJECT", projectPath.string().c_str(), true);
        success = success && SDL_SetEnvironmentVariable(environment, "CANIS_PROJECT_HUB", "0", true);
        success = success && SDL_SetEnvironmentVariable(environment, "CANIS_SKIP_PROJECT_HUB", "1", true);
        if (!success)
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

        success = SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<const char**>(args.data()));
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

    void OpenProject(AppState &_state, const fs::path &_path, bool &_running)
    {
        std::optional<fs::path> projectPath = NormalizeProjectPath(_path);
        if (!projectPath.has_value())
        {
            SetMessage(_state, "That folder is not a Canis project.", true);
            return;
        }

        std::string error;
        if (!LaunchProject(_state, *projectPath, error))
        {
            SetMessage(_state, error.empty() ? "Launch failed." : error, true);
            return;
        }

        AddRecentProject(_state, *projectPath);
        SaveConfig(_state);
        SetMessage(_state, "Launched " + projectPath->filename().string(), false);
        if (_state.closeAfterLaunch)
            _running = false;
    }

    void DrawCanisPack(AppState &_state, bool &_running)
    {
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

            const std::string label = projectPath.filename().string() + "##" + projectPath.generic_string();
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
        ImGui::InputText("Location", &_state.projectLocation);
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
        if (ImGui::Button("Create and Open", ImVec2(160.0f, 34.0f)))
        {
            fs::path projectPath;
            std::string error;
            if (CreateProject(_state, projectPath, error))
                OpenProject(_state, projectPath, _running);
            else
                SetMessage(_state, error.empty() ? "Could not create project." : error, true);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Open Existing");
        ImGui::InputText("Project Path", &_state.openProjectPath);
        if (ImGui::Button("Open Project", ImVec2(140.0f, 34.0f)))
            OpenProject(_state, _state.openProjectPath, _running);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Engine");
        ImGui::InputText("Executable", &_state.engineExecutable);
        if (ImGui::Checkbox("Close after launch", &_state.closeAfterLaunch))
            SaveConfig(_state);

        if (!_state.message.empty())
        {
            ImGui::Spacing();
            const ImVec4 color = _state.messageIsError ? ImVec4(1.0f, 0.36f, 0.30f, 1.0f) : ImVec4(0.40f, 0.82f, 0.50f, 1.0f);
            ImGui::TextColored(color, "%s", _state.message.c_str());
        }

        ImGui::EndChild();
        ImGui::End();
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

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    AppState state;
    state.projectLocation = GetDefaultProjectsDirectory().generic_string();
    LoadConfig(state);
    if (const char *engineExecutable = std::getenv("CANIS_ENGINE_EXECUTABLE"))
        state.engineExecutable = engineExecutable;
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

        DrawCanisPack(state, running);

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

    SaveConfig(state);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
