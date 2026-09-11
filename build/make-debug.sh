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
# there); no path inside it is something a user needs. Same overlay/timestamp
# handling as make-release.sh; see that script for why it is needed.
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

overlay="$root/build/vcpkg-root"
if [ ! -e "$overlay/.vcpkg-root" ]; then
  mkdir -p "$overlay"/{buildtrees,downloads,packages,installed}
  for part in scripts ports triplets versions; do
    [ -e "$overlay/$part" ] || ln -s "$VCPKG_ROOT/$part" "$overlay/$part" 2>/dev/null || true
  done
  touch "$overlay/.vcpkg-root"
fi
if [ ! -d "$overlay/installed/x64-linux/share" ] && [ -d build/dev/vcpkg_installed/x64-linux ]; then
  cp -a build/dev/vcpkg_installed/x64-linux "$overlay/installed/x64-linux"
fi

CCACHE_DISABLE=1 VCPKG_ROOT="$overlay" cmake -S . -B build/dev -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$overlay/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_INSTALLED_DIR="$overlay/installed" \
  -DVCPKG_MANIFEST_INSTALL=OFF >/dev/null

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
