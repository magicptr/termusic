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
# Why the overlay/touch dance instead of a bare `cmake --build`:
#  * vcpkg takes a lock on its own root during a reconfigure, and that root is
#    not necessarily writable, so build/vcpkg-root is a writable overlay of it
#    (see build/vcpkg-env.sh);
#  * on this filesystem a write and an edit stamp different clocks, which makes
#    ninja's mtime comparison unreliable, so the sources are touched first.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

out="$root/build/termusic"


# 1. Delete the old published RELEASE artifact before compiling, so a failed
#    build cannot leave a stale executable at the official path. Build
#    directories (build/release, build/dev, or whatever they are called) are
#    internal: nothing there is user-facing.
rm -f "$out" build/release/termusic

# 2. Dependency bootstrap: find the real vcpkg, prepare the writable overlay.
#    The manifest install below is what puts FTXUI into this checkout, so a
#    fresh clone builds with no prepared state of any kind.
. "$root/build/vcpkg-env.sh"
termusic_vcpkg_setup "$root"

echo "configuring build/release (vcpkg $VCPKG_REAL_ROOT, overlay $VCPKG_OVERLAY)"
if ! CCACHE_DISABLE=1 VCPKG_ROOT="$VCPKG_OVERLAY" cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN" \
  -DVCPKG_INSTALLED_DIR="$VCPKG_INSTALLED" \
  -DVCPKG_MANIFEST_INSTALL=ON > build/release/configure.log 2>&1; then
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
