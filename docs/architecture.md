# CanisPack Architecture

CanisPack is a standalone CMake launcher for Canis projects.

## Standalone App

The standalone app lives in this repo and builds `CanisPack`.

Startup flow:

1. CanisPack loads recent projects from `user_settings/canispack.conf`.
2. A user creates or opens a project.
3. For new projects, CanisPack fetches release tags from CanisTemplate.
4. CanisPack clones the selected template tag into the new project folder.
5. CanisPack launches `c-engine` as a background process.
6. The selected project is passed with `CANIS_PROJECT=/path/to/project`.
7. The engine opens that project directly.

The standalone app uses small direct submodules:

```text
vendor/canis/
vendor/SDL/
vendor/imgui/
vendor/yaml-cpp/
```

Starter project assets live in `git@github.com:EricWRogers/CanisTemplate.git`. CanisPack fetches that repository's tags so each new project can choose a template release.

Useful environment variables:

- `CANIS_PROJECT=/path/to/project` opens a project directly.
- `CANIS_PROJECT_HUB=0` tells the engine not to show its own project picker.
- `CANIS_SKIP_PROJECT_HUB=1` keeps launch behavior direct.
- `CANIS_TEMPLATE_REPOSITORY=git@github.com:EricWRogers/CanisTemplate.git` overrides the default template repository.

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
