#!/usr/bin/env bash
# Build the dev tree. ccache and the workspace vcpkg overlay are both required
# in this sandbox: the real vcpkg root lives on a read-only path.
set -e
cd "$(dirname "$0")/.."
# Ninja's mtime comparison is unreliable on this filesystem (writes and edits
# stamp different clocks), so make every source strictly newer than its object.
find src tests -name '*.cpp' -o -name '*.hpp' | xargs touch
CCACHE_DISABLE=1 cmake --build build/dev "$@"
