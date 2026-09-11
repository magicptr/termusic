#!/usr/bin/env bash
# The standard build command for this checkout. Two official artifacts, and
# these two paths are the only ones a user ever needs:
#
#   ./build/make.sh            -> build/termusic        (release; run this)
#   ./build/make.sh --debug    -> build/termusic-debug  (assertions on)
#
# Each run deletes the old artifact OF ITS OWN VARIANT before compiling, so a
# failed build cannot leave a stale executable at the official path. The other
# variant's artifact is left in place: build/termusic and build/termusic-debug
# are independent, so building one never destroys the other. Every other
# executable in the tree is an internal build-directory output (build/release,
# build/dev, ...) and is NOT a path anyone needs to know or run.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"

case "${1:-}" in
  --debug)
    shift
    exec "$root/build/make-debug.sh" "$@"
    ;;
  --release|"")
    [ "${1:-}" = "--release" ] && shift
    exec "$root/build/make-release.sh" "$@"
    ;;
  -h|--help)
    sed -n '2,13p' "$0"
    exit 0
    ;;
  *)
    echo "usage: build/make.sh [--debug]" >&2
    exit 2
    ;;
esac
