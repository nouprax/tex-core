#!/bin/sh
set -eu

if [ ! -f CMakeLists.txt ]; then
    echo "note: no CMake build yet; lint:c is a no-op until Phase 2 lands the C core"
    exit 0
fi

BUILD_DIR=${TEX_CORE_C_LINT_BUILD_DIR:-build/lint-c}

cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DTEX_CORE_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD_DIR" --parallel
