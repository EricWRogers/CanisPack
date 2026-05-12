#!/usr/bin/env bash
set -euo pipefail

ENGINE_REPO="${CANIS_ENGINE_REPO:-/home/eric/Git/BathBattleCanis}"

cd "$ENGINE_REPO"
cmake --build build --parallel
CANIS_PROJECT_HUB=1 ./project/c-engine

