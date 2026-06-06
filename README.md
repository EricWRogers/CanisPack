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
- Build existing full workspaces before launching when their editor executable is missing.
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

When opening an existing full workspace, CanisPack accepts either the workspace root or the nested `project/` folder. If the workspace's editor executable is missing, CanisPack configures and builds the workspace before launching.

## Direction

CanisPack should eventually become a standalone launcher that can:

- Create new projects from templates.
- Open existing projects without changing terminal working directories by hand.
- Track recent projects.
- Manage Canis engine versions.
- Package or export projects.
- Install more templates and sample projects.
