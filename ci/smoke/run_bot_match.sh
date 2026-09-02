#!/usr/bin/env bash
# Dedicated-server gate: run q3ded with four bots for 60 seconds and check that it neither
# crashes nor logs an engine error.
#
# Usage (inside the development container): ci/smoke/run_bot_match.sh [BUILD_DIR] [VM_GAME]
#   VM_GAME 0 loads the native qagame module from the build; 1 loads the id QVM from pak0.pk3.
#
# Exit codes: 0 pass, 1 the server crashed or logged an error, 2 setup error.
set -euo pipefail

BUILD_DIR=${1:-build}
VM_GAME=${2:-0}
DURATION=${DURATION:-60}
MAP=${MAP:-q3dm7}

REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT_DIR=$REPO/ci/smoke/out
PAKS=${Q3_PAKS:-/paks}

fail() { echo "run_bot_match: $*" >&2; exit 2; }
[ -x "$BUILD_DIR/q3ded" ] || fail "no server binary at $BUILD_DIR/q3ded; run 'make build' first"
[ -f "$PAKS/pak0.pk3" ] || fail "no pak0.pk3 in $PAKS; set Q3_PAKS or put the paks in docker/paks/"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/base/baseq3" "$WORK/home" "$OUT_DIR"
ln -s "$PAKS"/*.pk3 "$WORK/base/baseq3/"
for module in "$BUILD_DIR"/qagame*.so "$BUILD_DIR"/baseq3/qagame*.so; do
    [ -e "$module" ] && ln -sf "$(readlink -f "$module")" "$WORK/base/baseq3/" || true
done

LOG=$OUT_DIR/bot_match_vm$VM_GAME.log
echo "run_bot_match: $DURATION s on $MAP with vm_game $VM_GAME (log: ci/smoke/out/$(basename "$LOG"))"
set +e
timeout "$DURATION" "$BUILD_DIR/q3ded" \
    +set dedicated 1 +set fs_basepath "$WORK/base" +set fs_homepath "$WORK/home" \
    +set net_port 27965 +set sv_pure 0 +set vm_game "$VM_GAME" \
    +set bot_enable 1 +set bot_minplayers 4 +set g_gametype 0 \
    +map "$MAP" \
    +addbot sarge 3 +addbot grunt 3 +addbot major 3 +addbot visor 3 \
    > "$LOG" 2>&1
STATUS=$?
set -e

# timeout(1) returns 124 when it stopped a still-running process, which is the pass condition.
if [ "$STATUS" -ne 124 ]; then
    echo "run_bot_match: server exited early with code $STATUS; last log lines:" >&2
    tail -n 20 "$LOG" >&2
    exit 1
fi
if grep -Eq 'Sys_Error|\*\*\*\*\*\*\*\* ERROR|Segmentation|Assertion' "$LOG"; then
    echo "run_bot_match: error in log:" >&2
    grep -En 'Sys_Error|ERROR|Segmentation|Assertion' "$LOG" | head >&2
    exit 1
fi
JOINED=$(grep -c 'entered the game' "$LOG" || true)
echo "run_bot_match: $JOINED bots entered the game"
if [ "$JOINED" -lt 4 ]; then
    echo "run_bot_match: fewer than 4 bots joined" >&2
    exit 1
fi
echo "run_bot_match: pass"
