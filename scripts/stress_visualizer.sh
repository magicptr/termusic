#!/usr/bin/env bash
# Visualizer stability harness.
#
# Usage:
#   scripts/stress_visualizer.sh [--duration 900] [--switches 200] \
#                                [--resize-cycles 100] [--binary build/debug/termusic]
#
# Exits non-zero on any assertion, sanitizer report, non-zero child exit or
# unexpected termination, so it can gate CI. Logs land in $LOGDIR.

set -uo pipefail

DURATION=30
SWITCHES=20
RESIZE_CYCLES=20
BINARY="build/debug/termusic"
LOGDIR="${LOGDIR:-${TMPDIR:-/tmp}/termusic-stress}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --duration) DURATION="$2"; shift 2 ;;
    --switches) SWITCHES="$2"; shift 2 ;;
    --resize-cycles) RESIZE_CYCLES="$2"; shift 2 ;;
    --binary) BINARY="$2"; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

mkdir -p "$LOGDIR"
LOG="$LOGDIR/run.log"
: > "$LOG"

# --- isolation: this harness must never touch real user data ----------------
# It runs a real TUI for minutes. Without an isolated HOME/XDG the app would
# read the developer's own ~/.config/termusic and could connect to whatever
# their configuration names -- on a workstation, their own server on :6600.
# FAIL CLOSED: there is no fallback to the real HOME or to the default port.
SAFE_HOME="${TERMUSIC_STRESS_HOME:-$(mktemp -d "${TMPDIR:-/tmp}/termusic-stress-home.XXXXXX")}"
SAFE_PORT="${TERMUSIC_STRESS_PORT:-6699}"
case "$SAFE_PORT" in
  ''|*[!0-9]*) echo "unsafe MPD port: '$SAFE_PORT'" >&2; exit 2 ;;
esac
if [[ "$SAFE_PORT" -eq 6600 || "$SAFE_PORT" -lt 1024 ]]; then
  echo "refusing MPD port $SAFE_PORT: the harness only targets a private port" >&2
  exit 2
fi
mkdir -p "$SAFE_HOME/config" "$SAFE_HOME/data" "$SAFE_HOME/cache" \
         "$SAFE_HOME/state" ||
  { echo "cannot create isolated home $SAFE_HOME" >&2; exit 2; }

fail() { echo "FAIL: $1" >&2; echo "FAIL: $1" >> "$LOG"; cleanup; exit 1; }

SESSION="termusic-stress-$$"
cleanup() {
  tmux kill-session -t "$SESSION" 2>/dev/null
  rm -rf "$SAFE_HOME"
}

command -v tmux >/dev/null || { echo "tmux is required" >&2; exit 2; }
[[ -x "$BINARY" ]] || { echo "binary not found: $BINARY" >&2; exit 2; }

cleanup
tmux new-session -d -s "$SESSION" -x 150 -y 44 \
  "ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
   HOME='$SAFE_HOME' XDG_CONFIG_HOME='$SAFE_HOME/config' \
   XDG_DATA_HOME='$SAFE_HOME/data' XDG_CACHE_HOME='$SAFE_HOME/cache' \
   XDG_STATE_HOME='$SAFE_HOME/state' \
   '$BINARY' --ui-visualizer-motion-test --host 127.0.0.1 --port '$SAFE_PORT' \
   >/dev/null 2>>'$LOG'" \
  || fail "could not start the harness session"

sleep 4
tmux has-session -t "$SESSION" 2>/dev/null || fail "app exited during startup"

# Collect the application pid for RSS sampling.
APP_PID="$(pgrep -n -f -- "--ui-visualizer-motion-test" || true)"
[[ -n "$APP_PID" ]] || fail "could not find the application process"

SIZES=("160 48" "128 40" "100 32" "80 24" "64 20" "48 19")
PAGES=(1 2)

echo "duration=${DURATION}s switches=${SWITCHES} resize_cycles=${RESIZE_CYCLES}" | tee -a "$LOG"

# --- page-switch stress -----------------------------------------------------
switches_done=0
for ((i = 0; i < SWITCHES; i++)); do
  tmux has-session -t "$SESSION" 2>/dev/null || fail "app died after $switches_done switches"
  tmux send-keys -t "$SESSION" "${PAGES[$((i % ${#PAGES[@]}))]}" 2>/dev/null
  sleep 0.2
  ((switches_done++))
done
echo "page switches: $switches_done" | tee -a "$LOG"

# --- resize stress ----------------------------------------------------------
resize_done=0
for ((i = 0; i < RESIZE_CYCLES; i++)); do
  tmux has-session -t "$SESSION" 2>/dev/null || fail "app died after $resize_done resizes"
  read -r w h <<< "${SIZES[$((i % ${#SIZES[@]}))]}"
  tmux resize-window -t "$SESSION" -x "$w" -y "$h" 2>/dev/null
  sleep 0.12
  ((resize_done++))
done
echo "resize cycles: $resize_done" | tee -a "$LOG"

# --- soak with RSS sampling -------------------------------------------------
start=$(date +%s)
first_rss=""
last_rss=""
while :; do
  now=$(date +%s)
  (( now - start >= DURATION )) && break
  tmux has-session -t "$SESSION" 2>/dev/null || fail "app died during the soak"
  rss=$(ps -o rss= -p "$APP_PID" 2>/dev/null | tr -d ' ')
  [[ -n "$rss" ]] || fail "application process disappeared during the soak"
  [[ -z "$first_rss" ]] && first_rss="$rss"
  last_rss="$rss"
  sleep 5
done
echo "RSS first=${first_rss}KB last=${last_rss}KB (pid $APP_PID)" | tee -a "$LOG"

cleanup

# --- verdict ----------------------------------------------------------------
if grep -qaE "Assertion|AddressSanitizer|runtime error:|ThreadSanitizer" "$LOG"; then
  grep -aE "Assertion|AddressSanitizer|runtime error:|ThreadSanitizer" "$LOG" | head -5 >&2
  fail "sanitizer report or assertion found in $LOG"
fi

if [[ -n "$first_rss" && -n "$last_rss" && "$last_rss" -gt $(( first_rss * 2 + 4096 )) ]]; then
  fail "RSS more than doubled (${first_rss}KB -> ${last_rss}KB); possible leak"
fi

echo "PASS: switches=$switches_done resizes=$resize_done duration=${DURATION}s"
echo "PASS" >> "$LOG"
exit 0
