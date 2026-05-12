#!/usr/bin/env bash
set -euo pipefail

git submodule update --init vendor/canis vendor/SDL vendor/imgui vendor/yaml-cpp
cmake -S . -B build
cmake --build build --parallel
