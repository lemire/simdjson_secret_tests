#!/usr/bin/env bash
# Stress tests for simdjson PR #2776 (key_selector / for_each / static reflection).
# Fetches simdjson branch lemire/selector7 via CPM; builds with GCC 16 in docker/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
ENABLE_REFLECTION="${ENABLE_REFLECTION:-ON}"
ENABLE_SANITIZERS="${ENABLE_SANITIZERS:-ON}"

CMAKE_ARGS=(
  -S .
  -B "$BUILD_DIR"
  -DENABLE_REFLECTION="$ENABLE_REFLECTION"
  -DENABLE_SANITIZERS="$ENABLE_SANITIZERS"
)

if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  echo "Building and running in gcc16 docker..."
  ./docker/run-docker-station \
    "python3 scripts/generate_headers.py && cmake ${CMAKE_ARGS[*]} && cmake --build $BUILD_DIR -j && ./$BUILD_DIR/key_selector_stress_tests"
else
  echo "Docker unavailable — building locally..."
  python3 scripts/generate_headers.py
  cmake "${CMAKE_ARGS[@]}"
  cmake --build "$BUILD_DIR" -j
  "./$BUILD_DIR/key_selector_stress_tests"
fi