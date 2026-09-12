#!/usr/bin/env bash
# Rebuilds the debug binary (assertions ON) and the test binaries, and
# publishes the application at build/termusic-debug -- the debug artifact.
#
#   ./build/make-debug.sh
#
# The previous artifact is DELETED first, so a failed build leaves nothing
# stale behind and every file that appears is freshly linked from the current
# source. The release artifact (build/termusic) is deliberately LEFT ALONE: the
# two published paths are independent, and building one variant must never
# destroy the other one's artifact.
#
# build/dev is an internal build directory (the test binaries ctest runs live
# there); no path inside it is something a user needs. Same dependency and
# timestamp handling as make-release.sh; see that script for why it is needed.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

out="$root/build/termusic-debug"


# 1. Delete the old published DEBUG artifact before compiling, so a failed
#    build cannot leave a stale executable at the official path, along with the
#    internal outputs this build directory would otherwise reuse.
rm -f "$out" \
      build/dev/termusic \
      build/dev/termusic_tests \
      build/dev/termusic_context_tests \
      build/dev/termusic_plugin_tests \
      build/dev/termusic_test_plugin.so

# Configure. The log lives in the build directory, which does not exist yet in
# a fresh clone (cmake creates it), so create it before the redirection.
# A build directory left over from the removed vcpkg toolchain would silently
# keep using it, so such a cache is discarded and configured from scratch.
if grep -q 'vcpkg' build/dev/CMakeCache.txt 2>/dev/null; then
  echo "discarding build/dev: it was configured with the removed vcpkg toolchain"
  rm -rf build/dev
fi
mkdir -p build/dev
echo "configuring build/dev (FTXUI from the system, or fetched once)"
if ! CCACHE_DISABLE=1 cmake -S . -B build/dev -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug > build/dev/configure.log 2>&1; then
  echo "configure failed; last lines of build/dev/configure.log:" >&2
  tail -n 25 build/dev/configure.log >&2
  exit 1
fi

find src tests -name '*.cpp' -o -name '*.hpp' | xargs touch
CCACHE_DISABLE=1 cmake --build build/dev "$@"


# 3. Publish the binary, then prove the new file is the one that was just
#    linked: the published artifact must be byte-identical to the executable
#    ninja produced in this run, and it must be executable.
cp -f build/dev/termusic "$out"
test -x "$out"
linked="$(md5sum build/dev/termusic | cut -d' ' -f1)"
published="$(md5sum "$out" | cut -d' ' -f1)"
if [ "$linked" != "$published" ]; then
  echo "publish failed: $out is not the freshly linked executable" >&2
  rm -f "$out"
  exit 1
fi
echo
echo "rebuilt: $out   (debug, assertions on; this is the user-facing path)"
echo "when:    $(date '+%Y-%m-%d %H:%M:%S')"
echo "size:    $(stat -c%s "$out") bytes"
echo "md5:     $published"
echo "internal: build/dev/termusic + build/dev/termusic_*_tests (ninja output;"
echo "          ctest runs them with: ctest --test-dir build/dev)"
