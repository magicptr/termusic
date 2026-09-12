#!/usr/bin/env bash
# ASan/UBSan build of the sanitizer tree, configured separately from build/dev.
set -e
cd "$(dirname "$0")/.."
find src tests -name '*.cpp' -o -name '*.hpp' | xargs touch
CCACHE_DISABLE=1 cmake --build build/sanitize "$@"
