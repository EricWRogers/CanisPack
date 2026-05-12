# CanisPack

CanisPack is the project launcher for Canis, inspired by Unity Hub and Godot's project manager.

The first implementation currently lives inside the Canis editor runtime in `BathBattleCanis/canis/src/ProjectHub.cpp`. This repository is the home for growing it into its own tool: project creation, project opening, recent projects, templates, and future package/version management.

## Current Features

- Create a Canis project folder.
- Open an existing Canis project.
- Remember recent projects.
- Scaffold required starter folders and files:
  - `assets/defaults/`
  - `assets/shaders/`
  - `assets/scenes/default.scene`
  - `project_settings/project.canis`

## Run The Current CanisPack Hub

From the engine repo:

```bash
cd /home/eric/Git/BathBattleCanis
cmake --build build --parallel
CANIS_PROJECT_HUB=1 ./project/c-engine
```

Or use the helper script from this repo:

```bash
./scripts/run-current-hub.sh
```

## Direction

CanisPack should eventually become a standalone launcher that can:

- Create new projects from templates.
- Open existing projects without changing terminal working directories by hand.
- Track recent projects.
- Manage Canis engine versions.
- Package or export projects.
- Install templates and sample projects.

