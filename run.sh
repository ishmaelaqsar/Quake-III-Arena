#!/usr/bin/env bash
# Launch the modern Quake III Arena client.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${REPO_DIR}/build"
CLIENT_BIN="${BUILD_DIR}/quake3_modern"

if [ ! -x "${CLIENT_BIN}" ]; then
    echo "Executable not found at ${CLIENT_BIN}. Building..."
    make -C "${REPO_DIR}" build
fi

exec "${CLIENT_BIN}" +set fs_basepath "${REPO_DIR}" "$@"
