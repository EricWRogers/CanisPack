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
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct AppState
    {
        std::vector<fs::path> recentProjects = {};
        std::string projectName = "New Canis Project";
        std::string projectLocation = "";
        std::string openProjectPath = "";
        std::string engineExecutable = CANISPACK_ENGINE_EXECUTABLE;
        std::string message = "";
        bool messageIsError = false;
        bool closeAfterLaunch = false;
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

    YAML::Node SequenceNode(std::initializer_list<double> _values)
    {
        YAML::Node node(YAML::NodeType::Sequence);
        for (double value : _values)
            node.push_back(value);
        return node;
    }

    std::uint64_t MakeId()
    {
        static std::random_device randomDevice;
        static std::mt19937_64 generator(randomDevice());
        const std::uint64_t value = generator();
        return value == 0u ? 1u : value;
    }

    YAML::Node AssetReferenceNode(const std::string &_path)
    {
        YAML::Node node(YAML::NodeType::Map);
        node["path"] = _path;
        return node;
    }

    YAML::Node TransformNode(
        std::initializer_list<double> _position,
        std::initializer_list<double> _rotation,
        std::initializer_list<double> _scale)
    {
        YAML::Node node(YAML::NodeType::Map);
        node["active"] = true;
        node["position"] = SequenceNode(_position);
        node["rotation"] = SequenceNode(_rotation);
        node["scale"] = SequenceNode(_scale);
        node["parent"] = 0;
        node["children"] = YAML::Node(YAML::NodeType::Sequence);
        return node;
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

    void LoadConfig(AppState &_state)
    {
        const fs::path configPath = GetConfigPath();
        if (!IsFile(configPath))
            return;

        try
        {
            const YAML::Node root = YAML::LoadFile(configPath.string());
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

    bool CopyDirectoryContents(const fs::path &_source, const fs::path &_destination, std::string &_outError)
    {
        std::error_code ec;
        if (!fs::exists(_source, ec) || !fs::is_directory(_source, ec))
        {
            _outError = "Template folder was not found: " + _source.generic_string();
            return false;
        }

        fs::create_directories(_destination, ec);
        if (ec)
        {
            _outError = "Could not create folder: " + _destination.generic_string();
            return false;
        }

        for (const fs::directory_entry &entry : fs::recursive_directory_iterator(_source, ec))
        {
            if (ec)
            {
                _outError = "Could not read template folder: " + _source.generic_string();
                return false;
            }

            const fs::path relativePath = fs::relative(entry.path(), _source, ec);
            if (ec)
            {
                _outError = "Could not resolve template path.";
                return false;
            }

            const fs::path destinationPath = _destination / relativePath;
            if (entry.is_directory(ec))
            {
                fs::create_directories(destinationPath, ec);
            }
            else if (entry.is_regular_file(ec))
            {
                fs::create_directories(destinationPath.parent_path(), ec);
                fs::copy_file(entry.path(), destinationPath, fs::copy_options::overwrite_existing, ec);
            }

            if (ec)
            {
                _outError = "Could not copy template item: " + entry.path().generic_string();
                return false;
            }
        }

        return true;
    }

    bool WriteProjectConfigFile(const fs::path &_projectPath, std::string &_outError)
    {
        YAML::Node root;
        root["useFrameLimit"] = false;
        root["frameLimit"] = 120;
        root["frameLimitEditor"] = 120;
        root["overrideSeed"] = false;
        root["seed"] = 0;
        root["volume"] = 1.0;
        root["musicVolume"] = 1.0;
        root["sfxVolume"] = 1.0;
        root["mute"] = false;
        root["editor"] = true;
        root["syncMode"] = 0;
        root["iconUUID"] = "0";
        root["launchScene"] = "assets/scenes/default.scene";
        root["launchExecutablePath"] = "";
        root["launchWorkingDirectory"] = "";
        root["launchArguments"] = "";
        root["editorWindowWidth"] = 1280;
        root["editorWindowHeight"] = 720;
        root["targetGameWidth"] = 1280;
        root["targetGameHeight"] = 720;

        const fs::path configPath = _projectPath / "project_settings" / "project.canis";
        std::error_code ec;
        fs::create_directories(configPath.parent_path(), ec);
        if (ec)
        {
            _outError = "Could not create project settings.";
            return false;
        }

        std::ofstream out(configPath);
        if (!out.is_open())
        {
            _outError = "Could not write project.canis.";
            return false;
        }

        out << root;
        return out.good();
    }

    bool WriteDefaultSceneFile(const fs::path &_projectPath, std::string &_outError)
    {
        const fs::path scenePath = _projectPath / "assets" / "scenes" / "default.scene";
        std::error_code ec;
        fs::create_directories(scenePath.parent_path(), ec);
        if (ec)
        {
            _outError = "Could not create scenes folder.";
            return false;
        }

        YAML::Node environmentNode(YAML::NodeType::Map);
        environmentNode["ClearColor"] = SequenceNode({ 0.12, 0.14, 0.17, 1.0 });
        environmentNode["AmbientLight"] = SequenceNode({ 0.24, 0.26, 0.32, 1.0 });
        environmentNode["AmbientLightIntensity"] = 1.0;
        environmentNode["PostProcessAsset"] = AssetReferenceNode("assets/defaults/postprocess/default.postprocess");

        YAML::Node cameraNode(YAML::NodeType::Map);
        cameraNode["Entity"] = MakeId();
        cameraNode["Name"] = "Camera";
        cameraNode["Tag"] = "MainCamera";
        cameraNode["Canis::Transform"] = TransformNode({ 0.0, 6.0, 14.0 }, { -0.38, 0.0, 0.0 }, { 1.0, 1.0, 1.0 });
        YAML::Node cameraComponent(YAML::NodeType::Map);
        cameraComponent["primary"] = true;
        cameraComponent["fovDegrees"] = 60.0;
        cameraComponent["nearClip"] = 0.1;
        cameraComponent["farClip"] = 300.0;
        cameraNode["Canis::Camera"] = cameraComponent;

        YAML::Node lightNode(YAML::NodeType::Map);
        lightNode["Entity"] = MakeId();
        lightNode["Name"] = "Directional Light";
        lightNode["Tag"] = "";
        YAML::Node lightComponent(YAML::NodeType::Map);
        lightComponent["enabled"] = true;
        lightComponent["color"] = SequenceNode({ 1.0, 1.0, 1.0, 1.0 });
        lightComponent["intensity"] = 1.0;
        lightComponent["direction"] = SequenceNode({ -0.4, -1.0, -0.25 });
        lightNode["Canis::DirectionalLight"] = lightComponent;

        YAML::Node cubeNode(YAML::NodeType::Map);
        cubeNode["Entity"] = MakeId();
        cubeNode["Name"] = "Cube";
        cubeNode["Tag"] = "";
        cubeNode["Canis::Transform"] = TransformNode({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 });
        YAML::Node materialComponent(YAML::NodeType::Map);
        materialComponent["color"] = SequenceNode({ 1.0, 1.0, 1.0, 1.0 });
        materialComponent["MaterialAsset"] = AssetReferenceNode("assets/defaults/materials/default.material");
        cubeNode["Canis::Material"] = materialComponent;
        YAML::Node modelComponent(YAML::NodeType::Map);
        modelComponent["color"] = SequenceNode({ 1.0, 1.0, 1.0, 1.0 });
        modelComponent["ModelAsset"] = AssetReferenceNode("assets/defaults/models/cube.glb");
        cubeNode["Canis::Model"] = modelComponent;

        YAML::Node entitiesNode(YAML::NodeType::Sequence);
        entitiesNode.push_back(cameraNode);
        entitiesNode.push_back(lightNode);
        entitiesNode.push_back(cubeNode);

        YAML::Node root(YAML::NodeType::Map);
        root["Environment"] = environmentNode;
        root["Entities"] = entitiesNode;

        YAML::Emitter out;
        out << root;

        std::ofstream file(scenePath);
        if (!file.is_open())
        {
            _outError = "Could not write starter scene.";
            return false;
        }

        file << out.c_str();
        return file.good();
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

        fs::create_directories(projectPath, ec);
        if (ec)
        {
            _outError = "Could not create project folder.";
            return false;
        }

        const fs::path templatePath = WeaklyCanonicalPath(CANISPACK_TEMPLATE_PROJECT_DIR);
        if (!CopyDirectoryContents(templatePath / "assets", projectPath / "assets", _outError))
            return false;

        if (!WriteProjectConfigFile(projectPath, _outError))
            return false;

        if (!WriteDefaultSceneFile(projectPath, _outError))
            return false;

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
    if (const char *engineExecutable = std::getenv("CANIS_ENGINE_EXECUTABLE"))
        state.engineExecutable = engineExecutable;
    LoadConfig(state);

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
