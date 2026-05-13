# CanisPack Architecture

CanisPack is a standalone CMake launcher for Canis projects.

## Standalone App

The standalone app lives in this repo and builds `CanisPack`. Installed builds place the executable under the install prefix's binary directory, app assets under `share/canispack/assets`, a desktop entry under `share/applications`, and the app icon under `share/icons/hicolor/32x32/apps`.

Startup flow:

1. CanisPack loads recent projects from `~/.config/canispack/canispack.conf`.
2. A user creates or opens a project.
3. For new projects, CanisPack fetches release tags from CanisTemplate.
4. CanisPack clones the selected template tag with submodules into the new project folder.
5. CanisPack configures and builds the new workspace.
6. CanisPack launches the new workspace's `project/c-engine` as a background process.
7. The selected Canis project folder is passed with `CANIS_PROJECT=/path/to/project`.
8. The engine opens that project directly.

The standalone app uses small direct submodules:

```text
vendor/canis/
vendor/SDL/
vendor/imgui/
vendor/yaml-cpp/
```

Starter workspaces live in `git@github.com:EricWRogers/CanisTemplate.git`. CanisPack fetches that repository's tags so each new project can choose a template release. After cloning, CanisPack creates a local `main` branch, removes the template `origin` remote from the new project's root repository, and runs CMake configure/build before launching the editor.

Useful environment variables:

- `CANIS_PROJECT=/path/to/project` opens a project directly.
- `CANIS_PROJECT_HUB=0` tells the engine not to show its own project picker.
- `CANIS_SKIP_PROJECT_HUB=1` keeps launch behavior direct.
- `CANIS_TEMPLATE_REPOSITORY=git@github.com:EricWRogers/CanisTemplate.git` overrides the default template repository.

## Later

Keep the small launch contract with Canis:

```bash
CANIS_PROJECT=/path/to/project /path/to/project/c-engine
```

That keeps CanisPack lightweight while each project workspace owns the editor/runtime it was built with.

## Project Shape

A Canis project is currently recognized by this minimum shape:

```text
project-name/
  assets/
  project_settings/
    project.canis
```

New template releases are full workspaces with the editable Canis project nested under `project/`:

```text
project-name/
  canis/
  cmake/
  external/
  game/
  project/
    assets/
    project_settings/
      project.canis
  scripts/
  .gitignore
  .gitmodules
  CMakeLists.txt
  README.md
```

CanisPack accepts either the workspace root or the nested `project/` folder when opening an existing project.
