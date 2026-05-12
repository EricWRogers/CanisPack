# CanisPack Architecture

CanisPack starts as an engine-integrated launcher and can later split into a standalone app.

## Phase 1: Integrated Hub

The current hub is compiled into `c-engine` for editor desktop builds.

Startup flow:

1. `App::Run()` checks `CANIS_PROJECT`.
2. If no project is provided, it opens CanisPack.
3. CanisPack returns a selected project path.
4. The engine switches the current working directory to that project.
5. The normal editor runtime initializes.

Useful environment variables:

- `CANIS_PROJECT=/path/to/project` opens a project directly.
- `CANIS_PROJECT_HUB=1` forces the hub.
- `CANIS_SKIP_PROJECT_HUB=1` keeps the old direct-open behavior.

## Phase 2: Standalone Launcher

Move the launcher UI and project creation code into this repo while keeping a small launch contract with Canis:

```bash
CANIS_PROJECT=/path/to/project /path/to/c-engine
```

The standalone app can remain lightweight and still use the installed Canis runtime to open projects.

## Project Shape

A Canis project is currently recognized by this minimum shape:

```text
project-name/
  assets/
  project_settings/
    project.canis
```

New projects should include default assets and shaders so the editor can immediately create cubes, sprites, materials, and starter scenes.

