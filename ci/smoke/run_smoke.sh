#!/usr/bin/env bash
# Gate G1: render a fixed frame of a demo headless and compare it with a golden image.
#
# Usage (inside the development container, normally through the Makefile):
#   ci/smoke/run_smoke.sh [--update-golden|--apitrace] BUILD_DIR
#
# Needs Q3_PAKS to point at a directory holding pak0.pk3.
#
# The gate checks all of the following, because any one of them passing alone would be
# meaningless:
#   - the built game modules are present, so the gate exercises this tree and not the bytecode
#     shipped inside pak0.pk3;
#   - the engine exits successfully;
#   - the engine reports the timedemo result, which is the only proof the demo actually played;
#   - enough frames were captured to reach the comparison frame;
#   - that frame matches ci/smoke/golden/smoke.tga exactly.
#
# Frame selection uses cl_avidemo, which writes one screenshot per rendered frame and only while
# the client is CA_ACTIVE (code/client/cl_main.c:2025-2029). The Nth captured frame is therefore
# the Nth frame of demo playback, independent of how many frames the map load took. An earlier
# version used `wait 200; screenshot`, where `wait` counts loading frames too, so the captured
# frame moved with disk cache state.
#
# Exit codes: 0 pass, 1 gate failure, 2 setup error, 3 the engine produced no usable frames.
set -euo pipefail

MODE=check
BUILD_DIR=""
for arg in "$@"; do
    case "$arg" in
        --update-golden) MODE=update ;;
        --apitrace)      MODE=apitrace ;;
        -*)              echo "run_smoke: unknown option $arg" >&2; exit 2 ;;
        *)               BUILD_DIR="$arg" ;;
    esac
done
BUILD_DIR=${BUILD_DIR:-build}

REPO=$(cd "$(dirname "$0")/../.." && pwd)
SMOKE_DIR="$REPO/ci/smoke"
OUT_DIR="$SMOKE_DIR/out"
GOLDEN="$SMOKE_DIR/golden/smoke.tga"
PAKS=${Q3_PAKS:-/paks}

# The frame to compare, counted from the first frame of demo playback, and the number of frames
# the run must produce to be considered complete.
COMPARE_FRAME=${COMPARE_FRAME:-120}
MIN_FRAMES=${MIN_FRAMES:-150}

fail()  { echo "run_smoke: $*" >&2; exit 2; }
gate()  { echo "run_smoke: GATE FAIL: $*" >&2; exit 1; }

[ -x "$BUILD_DIR/quake3_modern" ] || fail "no client binary at $BUILD_DIR/quake3_modern; run 'make build' first"
[ -f "$PAKS/pak0.pk3" ] || fail "no pak0.pk3 in $PAKS; set Q3_PAKS or put the paks in docker/paks/"
for tool in xvfb-run compare convert; do
    command -v "$tool" >/dev/null || fail "$tool not found; run inside the development container"
done
[ "$MODE" != apitrace ] || command -v apitrace >/dev/null || fail "apitrace not found"

# Assemble a throwaway game directory: the paks plus the modules this tree built.
WORK=$(mktemp -d)
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT
BASEPATH="$WORK/base"
HOMEPATH="$WORK/home"
SHOTS="$HOMEPATH/baseq3/screenshots"
mkdir -p "$BASEPATH/baseq3" "$HOMEPATH/baseq3" "$OUT_DIR"

shopt -s nullglob
paks=("$PAKS"/*.pk3)
[ ${#paks[@]} -gt 0 ] || fail "no .pk3 files in $PAKS"
ln -s "${paks[@]}" "$BASEPATH/baseq3/"

# The modules are required, not optional. Without them the engine silently falls back to the
# bytecode inside pak0.pk3 and the gate would pass without testing any of this tree's game code.
for base in qagame cgame ui; do
    found=""
    for candidate in "$BUILD_DIR/baseq3/$base"*.so "$BUILD_DIR/$base"*.so \
                     "$BUILD_DIR/baseq3/$base"*.dylib "$BUILD_DIR/$base"*.dylib; do
        [ -e "$candidate" ] || continue
        ln -sf "$(readlink -f "$candidate")" "$BASEPATH/baseq3/"
        found=$candidate
        break
    done
    [ -n "$found" ] || fail "no built $base module under $BUILD_DIR; the gate would silently test the bytecode in pak0.pk3 instead"
done
shopt -u nullglob

cat > "$HOMEPATH/baseq3/smoke.cfg" <<CFG
demo four
wait 250
quit
CFG

# Every value that changes rendered output is set explicitly, so the golden image does not
# depend on a leftover q3config.cfg.
ENGINE_ARGS=(
    +set fs_basepath "$BASEPATH" +set fs_homepath "$HOMEPATH"
    +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480
    +set r_picmip 1 +set r_texturebits 32 +set r_ext_compressed_textures 0
    +set r_swapInterval 0 +set r_gamma 1 +set r_overBrightBits 1
    +set s_initsound 0 +set com_introplayed 1 +set com_maxfps 0
    +set vm_game 0 +set vm_cgame 0 +set vm_ui 0
    +set timedemo 1 +set cl_avidemo 10 +set nextdemo quit
    +exec smoke.cfg
)
ENV_PREFIX=(env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy)
TRACE_PREFIX=()
[ "$MODE" != apitrace ] || TRACE_PREFIX=(apitrace trace --api gl -o "$OUT_DIR/smoke.trace")

LOG="$OUT_DIR/smoke.log"
echo "run_smoke: rendering with $BUILD_DIR/quake3_modern"
set +e
xvfb-run -a -s "-screen 0 1024x768x24" \
    "${ENV_PREFIX[@]}" timeout 300 "${TRACE_PREFIX[@]}" "$BUILD_DIR/quake3_modern" "${ENGINE_ARGS[@]}" \
    > "$LOG" 2>&1
STATUS=$?
set -e

# A crash after the frames were written must not read as a pass.
if [ "$STATUS" -ne 0 ]; then
    echo "run_smoke: engine exited with $STATUS; last log lines:" >&2
    tail -n 25 "$LOG" >&2
    [ "$STATUS" -ne 124 ] || gate "engine hit the 300 second timeout"
    gate "engine exited non-zero ($STATUS)"
fi

# CL_DemoCompleted prints this only in timedemo mode, so it proves the demo played to the end.
if ! grep -qE '[0-9]+ frames' "$LOG"; then
    echo "run_smoke: no timedemo result in the log; last log lines:" >&2
    tail -n 25 "$LOG" >&2
    gate "the demo did not run to completion"
fi
grep -E '[0-9]+ frames' "$LOG" | tail -n 1

shopt -s nullglob
frames=("$SHOTS"/*.tga)
shopt -u nullglob
FRAME_COUNT=${#frames[@]}
echo "run_smoke: captured $FRAME_COUNT frames"
if [ "$FRAME_COUNT" -lt "$MIN_FRAMES" ]; then
    tail -n 25 "$LOG" >&2
    exit 3
fi

# Sorted order is capture order, because the engine numbers the files sequentially.
IFS=$'\n' sorted=($(printf '%s\n' "${frames[@]}" | sort)); unset IFS
SELECTED=${sorted[$COMPARE_FRAME]}
echo "run_smoke: comparing frame $COMPARE_FRAME ($(basename "$SELECTED"))"
cp "$SELECTED" "$OUT_DIR/smoke.tga"
convert "$OUT_DIR/smoke.tga" "$OUT_DIR/smoke.png"

if [ "$MODE" = apitrace ]; then
    echo "run_smoke: GL call counts (fixed-function calls must reach 0 under r_glsl 1)"
    DUMP="$OUT_DIR/smoke.trace.txt"
    apitrace dump "$OUT_DIR/smoke.trace" > "$DUMP" 2>"$OUT_DIR/apitrace.err" \
        || fail "apitrace dump failed; see ci/smoke/out/apitrace.err. Counts of 0 would otherwise look like success."
    [ -s "$DUMP" ] || fail "apitrace produced an empty dump; counts of 0 would look like success"
    for call in glBegin glMatrixMode glTexEnvf glAlphaFunc glVertexPointer glDrawElements \
                glBufferSubData glUseProgram; do
        printf '  %-18s %s\n' "$call" "$(grep -cE "[[:space:]]$call\(" "$DUMP" || true)"
    done
    exit 0
fi

if [ "$MODE" = update ]; then
    mkdir -p "$(dirname "$GOLDEN")"
    cp "$OUT_DIR/smoke.tga" "$GOLDEN"
    convert "$GOLDEN" "${GOLDEN%.tga}.png"
    echo "run_smoke: golden updated at ci/smoke/golden/smoke.tga; commit it and say why"
    exit 0
fi

if [ ! -f "$GOLDEN" ]; then
    echo "run_smoke: no golden image yet. A candidate is at ci/smoke/out/smoke.png." >&2
    echo "run_smoke: inspect it, then run 'make smoke-update-golden' to accept it." >&2
    exit 1
fi

# compare writes the metric to stderr, and some builds render it as "0 (0)".
DIFF=$(compare -metric AE "$GOLDEN" "$OUT_DIR/smoke.tga" "$OUT_DIR/smoke-diff.png" 2>&1 || true)
DIFF_COUNT=${DIFF%% *}
echo "run_smoke: differing pixels: $DIFF"
if [ "$DIFF_COUNT" = "0" ]; then
    echo "run_smoke: gate G1 pass"
    exit 0
fi
gate "frame differs from the golden image; see ci/smoke/out/smoke-diff.png"
