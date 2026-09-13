#!/usr/bin/env bash
# Rebuilds the optimized `termusic` binary from scratch and publishes it at
# build/termusic -- the release artifact, and the ONLY release path a user
# needs to know.
#
#   ./build/make-release.sh
#
# The previous artifact is DELETED first, so a build that fails leaves no stale
# binary to be run by mistake, and the file that appears afterwards is always
# freshly linked from the current source.
#
# The debug artifact (build/termusic-debug) is deliberately LEFT ALONE: the two
# published paths are independent, and building one variant must never destroy
# the other one's artifact.
#
# Dependencies: FTXUI, libmpdclient and kissfft are all built from pinned
# sources and linked statically, so no development package is needed for any of
# them. FTXUI is used from a system package when exactly 7.0.3 is installed;
# otherwise CMake fetches the pinned releases on the first configure, which
# needs network access once. No package manager is involved.
#
# The sources are touched before building because on this filesystem a write and
# an edit stamp different clocks, which makes ninja's mtime comparison
# unreliable.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

out="$root/build/termusic"


# 1. Delete the old published RELEASE artifact before compiling, so a failed
#    build cannot leave a stale executable at the official path. Build
#    directories (build/release, build/dev, or whatever they are called) are
#    internal: nothing there is user-facing.
rm -f "$out" build/release/termusic

# 2. Configure. The log lives in the build directory, which does not exist yet
#    in a fresh clone (cmake creates it), so create it before the redirection.
# A build directory left over from the removed vcpkg toolchain would silently
# keep using it, so such a cache is discarded and configured from scratch.
if grep -q 'vcpkg' build/release/CMakeCache.txt 2>/dev/null; then
  echo "discarding build/release: it was configured with the removed vcpkg toolchain"
  rm -rf build/release
fi
mkdir -p build/release
echo "configuring build/release (FTXUI from the system, or fetched once)"
if ! CCACHE_DISABLE=1 cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release > build/release/configure.log 2>&1; then
  echo "configure failed; last lines of build/release/configure.log:" >&2
  tail -n 25 build/release/configure.log >&2
  exit 1
fi

find src tests -name '*.cpp' -o -name '*.hpp' | xargs touch
CCACHE_DISABLE=1 cmake --build build/release --target termusic "$@"


# 3. Publish the binary, then prove the new file is the one that was just
#    linked: the published artifact must be byte-identical to the executable
#    ninja produced in this run, and it must be executable.
cp -f build/release/termusic "$out"
test -x "$out"
linked="$(md5sum build/release/termusic | cut -d' ' -f1)"
published="$(md5sum "$out" | cut -d' ' -f1)"
if [ "$linked" != "$published" ]; then
  echo "publish failed: $out is not the freshly linked executable" >&2
  rm -f "$out"
  exit 1
fi
echo
echo "rebuilt: $out   (release; this is the user-facing path)"
echo "when:    $(date '+%Y-%m-%d %H:%M:%S')"
echo "size:    $(stat -c%s "$out") bytes"
echo "md5:     $published"
echo "internal: build/release/termusic (ninja output -- not a user-facing path)"
"$out" --version
