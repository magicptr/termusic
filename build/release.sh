#!/usr/bin/env bash
# Builds the release archive:
#
#   build/dist/termusic-<version>-linux-<arch>.tar.gz
#   build/dist/termusic-<version>-linux-<arch>.tar.gz.sha256
#
# The archive contains exactly what a user needs to run the client -- the
# binary, the README, the LICENSE and the reference configuration -- and nothing
# else: no build objects, no tests, no fixtures, no captures, no .git, no user
# configuration and no music.
#
#   ./build/release.sh              # release build + archive
#   ./build/release.sh --no-build   # archive the existing build/termusic
#
# The version comes from ONE place: `termusic --version`, which CMake fills in
# from `project(termusic VERSION ...)`. The archive name, the staging directory
# and the packaging metadata therefore cannot disagree.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

build=1
for argument in "$@"; do
  case "$argument" in
    --no-build) build=0 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "usage: build/release.sh [--no-build]" >&2; exit 2 ;;
  esac
done

if [ "$build" = 1 ]; then
  ./build/make.sh
fi

binary="$root/build/termusic"
if [ ! -x "$binary" ]; then
  echo "release.sh: $binary is missing or not executable; run ./build/make.sh" >&2
  exit 1
fi

version="$("$binary" --version | awk '{print $2}')"
if [ -z "$version" ]; then
  echo "release.sh: cannot read the version from $binary --version" >&2
  exit 1
fi

case "$(uname -m)" in
  x86_64|amd64) arch=x86_64 ;;
  aarch64|arm64) arch=aarch64 ;;
  *) arch="$(uname -m)" ;;
esac

name="termusic-$version-linux-$arch"
dist="$root/build/dist"
stage="$dist/$name"

rm -rf "$stage"
mkdir -p "$stage"

# The reference configuration is GENERATED, so it can never drift from the
# defaults the binary actually ships.
"$binary" --print-default-config > "$stage/config.example.toml"
# And it must be valid: an example that --check-config rejects is worse than no
# example at all.
"$binary" --config "$stage/config.example.toml" --check-config >/dev/null

cp -f "$binary" "$stage/termusic"
chmod 0755 "$stage/termusic"
cp -f README.md "$stage/README.md"
cp -f LICENSE "$stage/LICENSE"
# Third-party notices: FTXUI is statically linked, so its MIT notice has to
# travel with the binary. See the file for the linkage evidence.
cp -f "$root/THIRD_PARTY_LICENSES.md" "$stage/THIRD_PARTY_LICENSES.md"
# Keep the packaged copy in the tree in step with the generated one.
cp -f "$stage/config.example.toml" "$root/packaging/config.example.toml"

# The packaging metadata carries the version too (a spec and a changelog need a
# literal). Rewriting it here from the SAME source keeps four files from drifting
# apart: CMakeLists.txt -> --version -> archive name -> spec/changelog.
python3 - "$version" "$root" <<'PYTHON'
import pathlib
import re
import sys

version, root = sys.argv[1], pathlib.Path(sys.argv[2])
spec = root / "packaging/termusic.spec"
text = spec.read_text()
text, count = re.subn(r"^Version:\s+\S+$", f"Version:        {version}", text,
                      count=1, flags=re.M)
if count:
    spec.write_text(text)
changelog = root / "packaging/debian/changelog"
text = changelog.read_text()
text, count = re.subn(r"^termusic \([^)]*\)", f"termusic ({version}-1)", text,
                      count=1, flags=re.M)
if count:
    changelog.write_text(text)
PYTHON
# And prove it: a packager must be able to trust the metadata.
if ! grep -q "^Version:        $version\$" "$root/packaging/termusic.spec"; then
  echo "release.sh: packaging/termusic.spec does not carry version $version" >&2
  exit 1
fi
if ! head -1 "$root/packaging/debian/changelog" | grep -q "^termusic ($version-1)"; then
  echo "release.sh: packaging/debian/changelog does not carry version $version" >&2
  exit 1
fi

# Deterministic archive: sorted entries and a fixed mtime, so the same source
# produces the same bytes.
tar --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime='2026-01-01 00:00:00 UTC' \
    -C "$dist" -czf "$dist/$name.tar.gz" "$name"

( cd "$dist" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256" )

echo
echo "archive:  $dist/$name.tar.gz"
echo "size:     $(stat -c%s "$dist/$name.tar.gz") bytes"
echo "sha256:   $(cut -d' ' -f1 < "$dist/$name.tar.gz.sha256")"
echo "version:  $version (from $binary --version)"
echo "contents:"
tar -tzf "$dist/$name.tar.gz" | sed 's/^/  /'
