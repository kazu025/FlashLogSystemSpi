#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"

usage(){
    echo "Usage: $0 [clean]"
    echo "  (no args) : incremental build"
    echo "  clean     : remove build dir then configure & build"
}
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$BUILD_DIR"
elif  [[ -n "${1:-}" ]]; then
    echo "Unknown arg: $1"
    usage
    exit 1
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make -j"$(nproc)"
