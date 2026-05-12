# CanisPack

CanisPack is the project launcher for Canis, inspired by Unity Hub and Godot's project manager.

This repository builds a standalone launcher with CMake. It keeps dependencies small: `vendor/canis` for the engine source reference, plus SDL, ImGui, and yaml-cpp as direct submodules.

## Current Features

- Create a Canis project folder.
- Open an existing Canis project.
- Remember recent projects.
- Scaffold required starter folders and files:
  - `assets/defaults/`
  - `assets/shaders/`
  - `assets/scenes/default.scene`
  - `project_settings/project.canis`

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

Starter project files live in `templates/basic`, so CanisPack does not need to vendor a whole game project just to create new projects.

## Direction

CanisPack should eventually become a standalone launcher that can:

- Create new projects from templates.
- Open existing projects without changing terminal working directories by hand.
- Track recent projects.
- Manage Canis engine versions.
- Package or export projects.
- Install templates and sample projects.
