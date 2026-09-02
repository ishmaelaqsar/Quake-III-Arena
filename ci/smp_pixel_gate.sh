#!/usr/bin/env bash
# Compare every frame between two engine runs that differ by one cvar.
#
# Usage:
#   ci/smp_pixel_gate.sh [--demo <name>] [BUILD_DIR] <cvar1> <val1> [<cvar2>] <val2>
#
# Examples:
#   ci/smp_pixel_gate.sh build r_picmip 1 r_picmip 1
#   ci/smp_pixel_gate.sh build r_picmip 1 r_picmip 2
#   ci/smp_pixel_gate.sh build r_smp 0 1
#
# Exit codes:
#   0: all frames match
#   1: frame difference detected
#   2: setup or argument error
#   3: engine did not produce screenshots
set -euo pipefail

DEMO="four"
while [ $# -gt 0 ]; do
    case "$1" in
        --demo)
            [ $# -ge 2 ] || { echo "smp_pixel_gate: --demo requires an argument" >&2; exit 2; }
            DEMO="$2"
            shift 2
            ;;
        *)
            break
            ;;
    esac
done

BUILD_DIR="build"
if [ $# -eq 5 ]; then
    BUILD_DIR="$1"
    CVAR1="$2"
    VAL1="$3"
    CVAR2="$4"
    VAL2="$5"
elif [ $# -eq 4 ]; then
    if [ -d "$1" ] || [ "$1" = "build" ]; then
        BUILD_DIR="$1"
        CVAR1="$2"
        VAL1="$3"
        CVAR2="$2"
        VAL2="$4"
    else
        CVAR1="$1"
        VAL1="$2"
        CVAR2="$3"
        VAL2="$4"
    fi
elif [ $# -eq 3 ]; then
    CVAR1="$1"
    VAL1="$2"
    CVAR2="$1"
    VAL2="$3"
else
    echo "Usage: ci/smp_pixel_gate.sh [--demo <name>] [BUILD_DIR] <cvar1> <val1> [<cvar2>] <val2>" >&2
    exit 2
fi

REPO=$(cd "$(dirname "$0")/.." && pwd)
SMOKE_DIR=$REPO/ci/smoke
OUT_DIR=$SMOKE_DIR/out/smp_gate
PAKS=${Q3_PAKS:-/paks}

fail() { echo "smp_pixel_gate: $*" >&2; exit 2; }

[ -x "$BUILD_DIR/quake3_modern" ] || fail "no client binary at $BUILD_DIR/quake3_modern; run 'make build' first"
[ -f "$PAKS/pak0.pk3" ] || fail "no pak0.pk3 in $PAKS; set Q3_PAKS or put the paks in docker/paks/"
command -v xvfb-run >/dev/null || fail "xvfb-run not found; run inside the development container"
command -v compare  >/dev/null || fail "ImageMagick compare not found"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

BASEPATH=$WORK/base
mkdir -p "$BASEPATH/baseq3" "$OUT_DIR"
ln -s "$PAKS"/*.pk3 "$BASEPATH/baseq3/"
for module in "$BUILD_DIR"/qagame*.so "$BUILD_DIR"/cgame*.so "$BUILD_DIR"/ui*.so \
              "$BUILD_DIR"/baseq3/qagame*.so "$BUILD_DIR"/baseq3/cgame*.so "$BUILD_DIR"/baseq3/ui*.so; do
    [ -e "$module" ] && ln -sf "$(readlink -f "$module")" "$BASEPATH/baseq3/" || true
done

run_pass() {
    local pass_id="$1"
    local cvar="$2"
    local val="$3"
    local homepath="$WORK/home_$pass_id"
    local shots_dir="$homepath/baseq3/screenshots"
    local log_file="$OUT_DIR/run_${pass_id}.log"

    mkdir -p "$homepath/baseq3"

    cat <<EOF > "$homepath/baseq3/avidemo.cfg"
demo $DEMO
wait 100
quit
EOF

    local engine_args=(
        +set fs_basepath "$BASEPATH" +set fs_homepath "$homepath"
        +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480
        +set r_picmip 1 +set r_texturebits 32 +set r_ext_compressed_textures 0
        +set r_swapInterval 0 +set r_gamma 1 +set r_overBrightBits 1
        +set s_initsound 0 +set com_introplayed 1 +set com_maxfps 0
        +set timedemo 1 +set cl_avidemo 10 +set nextdemo quit
        +set "$cvar" "$val"
        +exec avidemo.cfg
    )

    local env_prefix=(env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy)

    echo "smp_pixel_gate: starting pass $pass_id ($cvar=$val)"
    set +e
    xvfb-run -a -s "-screen 0 1024x768x24" \
        "${env_prefix[@]}" timeout 300 "$BUILD_DIR/quake3_modern" "${engine_args[@]}" \
        > "$log_file" 2>&1
    local status=$?
    set -e

    if [ ! -d "$shots_dir" ] || [ -z "$(ls -A "$shots_dir"/*.tga 2>/dev/null)" ]; then
        echo "smp_pixel_gate: pass $pass_id did not produce screenshots; last log lines:" >&2
        tail -n 20 "$log_file" >&2
        exit 3
    fi
}

run_pass 1 "$CVAR1" "$VAL1"
run_pass 2 "$CVAR2" "$VAL2"

SHOTS1=("$WORK/home_1/baseq3/screenshots"/*.tga)
TOTAL=${#SHOTS1[@]}
echo "smp_pixel_gate: comparing $TOTAL frames"

for file1 in "${SHOTS1[@]}"; do
    filename=$(basename "$file1")
    file2="$WORK/home_2/baseq3/screenshots/$filename"

    if [ ! -f "$file2" ]; then
        echo "smp_pixel_gate: missing frame $filename in pass 2" >&2
        exit 1
    fi

    diff_file="$OUT_DIR/diff_$filename.png"
    diff_count=$(compare -metric AE "$file1" "$file2" "$diff_file" 2>&1 || true)

    if [ "$diff_count" != "0" ]; then
        frame_num=$(echo "$filename" | sed -E 's/[^0-9]+//g')
        echo "smp_pixel_gate: frame $frame_num differed: $diff_count pixels (see $diff_file)" >&2
        exit 1
    fi
done

echo "smp_pixel_gate: all $TOTAL frames match (0 differing pixels)"
exit 0
