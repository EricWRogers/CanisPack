# CanisPack

CanisPack is the project launcher for Canis, inspired by Unity Hub and Godot's project manager.

This repository builds a standalone launcher with CMake. It keeps dependencies small: `vendor/canis` for the engine source reference, plus SDL, ImGui, and yaml-cpp as direct submodules.

## Current Features

- Create a Canis project folder.
- Open an existing Canis project.
- Remember recent projects.
- Clone a tagged CanisTemplate release, including engine submodules, for new projects.
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

Run:

```bash
./scripts/run.sh
```

Set the engine executable in the UI, or pass it when launching:

```bash
CANIS_ENGINE_EXECUTABLE=/path/to/c-engine ./build/bin/CanisPack
```

New projects are cloned from:

```text
git@github.com:EricWRogers/CanisTemplate.git
```

You can override that in the UI or with:

```bash
CANIS_TEMPLATE_REPOSITORY=git@github.com:EricWRogers/CanisTemplate.git ./build/bin/CanisPack
```

Created projects use the full CanisTemplate workspace shape. CanisPack keeps the new folder as a git project, removes the template `origin` remote, and launches the nested `project/` folder in the editor.

## Direction

CanisPack should eventually become a standalone launcher that can:

- Create new projects from templates.
- Open existing projects without changing terminal working directories by hand.
- Track recent projects.
- Manage Canis engine versions.
- Package or export projects.
- Install more templates and sample projects.
