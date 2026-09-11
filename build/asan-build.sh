#!/usr/bin/env bash
# ASan/UBSan build of the dev tree (same overlay vcpkg root as dev-build.sh).
set -e
cd "$(dirname "$0")/.."
find src tests -name '*.cpp' -o -name '*.hpp' | xargs touch
CCACHE_DISABLE=1 cmake --build build/sanitize "$@"
