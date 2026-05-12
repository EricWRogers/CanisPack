# CanisPack Architecture

CanisPack is a standalone CMake launcher for Canis projects.

## Standalone App

The standalone app lives in this repo and builds `CanisPack`.

Startup flow:

1. CanisPack loads recent projects from `user_settings/canispack.conf`.
2. A user creates or opens a project.
3. CanisPack launches `c-engine` as a background process.
4. The selected project is passed with `CANIS_PROJECT=/path/to/project`.
5. The engine opens that project directly.

The standalone app uses small direct submodules:

```text
vendor/canis/
vendor/SDL/
vendor/imgui/
vendor/yaml-cpp/
```

Starter project assets live in `templates/basic/`. CanisPack should not depend on a full game repository.

Useful environment variables:

- `CANIS_PROJECT=/path/to/project` opens a project directly.
- `CANIS_PROJECT_HUB=0` tells the engine not to show its own project picker.
- `CANIS_SKIP_PROJECT_HUB=1` keeps launch behavior direct.

## Later

Keep the small launch contract with Canis:

```bash
CANIS_PROJECT=/path/to/project /path/to/c-engine
```

That keeps CanisPack lightweight while Canis owns the editor/runtime.

## Project Shape

A Canis project is currently recognized by this minimum shape:

```text
project-name/
  assets/
  project_settings/
    project.canis
```

New projects should include default assets and shaders so the editor can immediately create cubes, sprites, materials, and starter scenes.
