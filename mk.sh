#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
CMAKE_OPTIONS="-DRUN_PSEUDO_POWER_CUT_TEST=OFF"
DO_CLEAN=0

usage(){
    echo "Usage: $0 [clean]"
    echo "  (no args) : incremental normal application build"
    echo "  clean     : remove build dir then configure & build"
    echo "  test_powercut : build mode is pseudo powercut test"
    echo ""
    echo "Examples:"
    echo "  $0"
    echo "  $0 clean"
    echo "  $0 test_powercut"
    echo "  $0 clean test_powercut"
}

for arg in "$@"; do
    case "$arg" in
        clean)
            DO_CLEAN=1
            ;;
        test_powercut)
            CMAKE_OPTIONS="-DRUN_PSEUDO_POWER_CUT_TEST=ON"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown arg: $arg"
            exit 1
            ;;
    esac
done

if [[ "$DO_CLEAN" -eq 1 ]]; then
    echo "remove build directory"
    rm -rf "$BUILD_DIR"
fi

if [[ "$CMAKE_OPTIONS" == *"ON"* ]]; then
    echo "build mode: pseudo power cut test"
else
    echo "build mode: normal application"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. ${CMAKE_OPTIONS}
make -j"$(nproc)"
