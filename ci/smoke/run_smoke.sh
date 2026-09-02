#!/usr/bin/env bash
# Gate G1: headless render of a fixed timedemo frame, compared with a golden image.
#
# Usage (inside the development container, see the Makefile):
#   ci/smoke/run_smoke.sh [--update-golden | --apitrace] [BUILD_DIR]
#
# The script needs Q3_PAKS to point at a directory that holds pak0.pk3. It plays
# `timedemo 1; demo four` under Xvfb with Mesa llvmpipe at a fixed configuration, takes a
# screenshot after a fixed number of rendered frames, and compares it with
# ci/smoke/golden/smoke.tga using ImageMagick. The gate passes when zero pixels differ.
#
# Exit codes: 0 pass, 1 pixel difference, 2 setup error, 3 engine did not produce a screenshot.
set -euo pipefail

MODE=check
case "${1:-}" in
    --update-golden) MODE=update; shift ;;
    --apitrace)      MODE=apitrace; shift ;;
esac
BUILD_DIR=${1:-build}

REPO=$(cd "$(dirname "$0")/../.." && pwd)
SMOKE_DIR=$REPO/ci/smoke
OUT_DIR=$SMOKE_DIR/out
GOLDEN=$SMOKE_DIR/golden/smoke.tga
PAKS=${Q3_PAKS:-/paks}

fail() { echo "run_smoke: $*" >&2; exit 2; }

[ -x "$BUILD_DIR/quake3_modern" ] || fail "no client binary at $BUILD_DIR/quake3_modern; run 'make build' first"
[ -f "$PAKS/pak0.pk3" ] || fail "no pak0.pk3 in $PAKS; set Q3_PAKS or put the paks in docker/paks/"
command -v xvfb-run >/dev/null || fail "xvfb-run not found; run inside the development container"
command -v compare  >/dev/null || fail "ImageMagick compare not found"

# Assemble a throwaway game directory: paks and the built modules, nothing from the host home.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
BASEPATH=$WORK/base
HOMEPATH=$WORK/home
mkdir -p "$BASEPATH/baseq3" "$HOMEPATH/baseq3"
ln -s "$PAKS"/*.pk3 "$BASEPATH/baseq3/"
for module in "$BUILD_DIR"/qagame*.so "$BUILD_DIR"/cgame*.so "$BUILD_DIR"/ui*.so \
              "$BUILD_DIR"/baseq3/qagame*.so "$BUILD_DIR"/baseq3/cgame*.so "$BUILD_DIR"/baseq3/ui*.so; do
    [ -e "$module" ] && ln -sf "$(readlink -f "$module")" "$BASEPATH/baseq3/" || true
done
cp "$SMOKE_DIR/smoke.cfg" "$HOMEPATH/baseq3/smoke.cfg"
mkdir -p "$OUT_DIR"

# Fixed configuration. Every value that changes rendered output is set explicitly so that the
# golden image does not depend on a q3config.cfg. The frame is selected by the `wait` count in
# smoke.cfg; in timedemo mode the engine renders one frame per demo message, so the count is
# deterministic for a given demo. `nextdemo quit` ends the run even if smoke.cfg fails.
ENGINE_ARGS=(
    +set fs_basepath "$BASEPATH" +set fs_homepath "$HOMEPATH"
    +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480
    +set r_picmip 1 +set r_texturebits 32 +set r_ext_compressed_textures 0
    +set r_swapInterval 0 +set r_gamma 1 +set r_overBrightBits 1
    +set s_initsound 0 +set com_introplayed 1 +set com_maxfps 0
    +set timedemo 1 +set nextdemo quit
    +exec smoke.cfg
)

ENV_PREFIX=(env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy)
TRACE_PREFIX=()
if [ "$MODE" = apitrace ]; then
    command -v apitrace >/dev/null || fail "apitrace not found"
    TRACE_PREFIX=(apitrace trace --api gl -o "$OUT_DIR/smoke.trace")
fi

echo "run_smoke: rendering with $BUILD_DIR/quake3_modern"
set +e
xvfb-run -a -s "-screen 0 1024x768x24" \
    "${ENV_PREFIX[@]}" timeout 300 "${TRACE_PREFIX[@]}" "$BUILD_DIR/quake3_modern" "${ENGINE_ARGS[@]}" \
    > "$OUT_DIR/smoke.log" 2>&1
STATUS=$?
set -e
echo "run_smoke: engine exit code $STATUS (log: ci/smoke/out/smoke.log)"

SHOT=$HOMEPATH/baseq3/screenshots/smoke.tga
if [ ! -f "$SHOT" ]; then
    echo "run_smoke: no screenshot produced; last log lines:" >&2
    tail -n 20 "$OUT_DIR/smoke.log" >&2
    exit 3
fi
cp "$SHOT" "$OUT_DIR/smoke.tga"
convert "$OUT_DIR/smoke.tga" "$OUT_DIR/smoke.png"
grep -E 'fps|frames' "$OUT_DIR/smoke.log" | tail -n 2 || true

if [ "$MODE" = apitrace ]; then
    echo "run_smoke: GL call counts (fixed-function calls must reach 0 under r_glsl 1)"
    for call in glBegin glMatrixMode glTexEnvf glAlphaFunc glVertexPointer glDrawElements \
                glBufferSubData glUseProgram; do
        count=$(apitrace dump --calls='*' "$OUT_DIR/smoke.trace" 2>/dev/null | grep -c "^[0-9]* $call(" || true)
        printf '  %-18s %s\n' "$call" "$count"
    done
    exit 0
fi

if [ "$MODE" = update ]; then
    mkdir -p "$(dirname "$GOLDEN")"
    cp "$OUT_DIR/smoke.tga" "$GOLDEN"
    convert "$GOLDEN" "${GOLDEN%.tga}.png"
    echo "run_smoke: golden updated at ci/smoke/golden/smoke.tga; commit it with the reason"
    exit 0
fi

if [ ! -f "$GOLDEN" ]; then
    echo "run_smoke: no golden image yet; candidate saved at ci/smoke/out/smoke.png." >&2
    echo "run_smoke: inspect it, then run 'make smoke-update-golden' to accept it." >&2
    exit 1
fi

DIFF=$(compare -metric AE "$GOLDEN" "$OUT_DIR/smoke.tga" "$OUT_DIR/smoke-diff.png" 2>&1 || true)
echo "run_smoke: differing pixels: $DIFF"
if [ "$DIFF" = "0" ]; then
    echo "run_smoke: gate G1 pass"
    exit 0
fi
echo "run_smoke: gate G1 FAIL; see ci/smoke/out/smoke-diff.png" >&2
exit 1
