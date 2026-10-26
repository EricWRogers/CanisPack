# CanisPack

CanisPack is the project launcher for Canis.

This repository builds a standalone launcher with CMake. It keeps dependencies small: `vendor/canis` for the engine source reference, plus SDL, ImGui, and yaml-cpp as direct submodules.

## Current Features

- Create a Canis project folder.
- Open an existing Canis project.
- Remember recent projects.
- Clone a tagged CanisTemplate release, including engine submodules, for new projects.
- Show clone/build progress while creating projects.
- Build newly created projects before launching their editor.
- Choose Release (the default), Debug, Release with debug symbols, or a project CMake preset.
- Remember the build choice for each project and build it before launch.
- Launch projects using the editor executable built inside each project workspace.
- Choose the template release from the available Git tags.

## Setup

Initialize submodules:

```bash
git submodule update --init vendor/canis vendor/SDL vendor/imgui vendor/yaml-cpp
```

Build:

```bash
./scripts/build.sh
```

Install to a prefix:

```bash
cmake --install build --prefix /usr/local
```

Run:

```bash
./scripts/run.sh
```

CanisPack stores user settings in:

```text
~/.config/canispack/canispack.conf
```

New projects are cloned from CanisTemplate. The Create Project panel can switch between SSH:

```text
git@github.com:EricWRogers/CanisTemplate.git
```

and HTTPS:

```text
https://github.com/EricWRogers/CanisTemplate.git
```

You can override the repository and clone protocol in the UI or with:

```bash
CANIS_TEMPLATE_REPOSITORY=git@github.com:EricWRogers/CanisTemplate.git CANIS_TEMPLATE_CLONE_PROTOCOL=ssh ./build/bin/CanisPack
```

Created projects use the full CanisTemplate workspace shape. CanisPack keeps the new folder as a git project, removes the template `origin` remote, builds the workspace, and launches the nested `project/` folder with the new workspace's editor executable.

When opening an existing full workspace, CanisPack accepts either the workspace root or the nested `project/` folder. Select a recent project (or browse to one), choose **Build Target**, then **Open Project**.
Double-clicking a recent project opens it with its remembered target. New projects use Release.
CanisPack configures and incrementally builds the selected target before every launch,
even when another editor executable already exists.

Project presets come from `cmake --list-presets=configure`, including `CMakeUserPresets.json`,
included presets, inheritance, and host conditions. All available configure presets are listed;
presets that disable the desktop editor (such as web export presets) cannot open an editor.
Use the project's export tools for those builds. A single-configuration preset that omits
`CMAKE_BUILD_TYPE` defaults to Release.

Each choice has its own cache under `build/canispack/` and, on current Canis templates,
its own executable and libraries under that build's `runtime/` directory. CMake's artifact
metadata identifies the actual editor target; unrelated test executables are not built.
Older templates that force a shared runtime directory are rebuilt cleanly before launch
to avoid mixing configurations. Standalone projects without CMake sources still launch
their packaged editor directly.

The launcher reads `executableName` from the workspace's `project_settings/project.canis`,
then falls back to the runtime project's settings if that name is missing or invalid.
Projects without a configured name use `c-engine`. An explicit custom name is never
replaced by a leftover `c-engine` binary; the configured executable must be built.

Run executable-discovery and build-selection integration tests with:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target CanisPackProjectExecutableTests CanisPackProjectBuildTests
ctest --test-dir build --output-on-failure
```

## Direction

CanisPack should eventually become a standalone launcher that can:

- Create new projects from templates.
- Open existing projects without changing terminal working directories by hand.
- Track recent projects.
- Manage Canis engine versions.
- Package or export projects.
- Install more templates and sample projects.
