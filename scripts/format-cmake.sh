#!/bin/sh
set -eu

VERSION="0.6.13"
TOOL_DIR="${CMAKE_FORMAT_TOOL_DIR:-$PWD/.tools/cmakelang/$VERSION}"
VENV="$TOOL_DIR/venv"

if [ ! -x "$VENV/bin/cmake-format" ] || ! "$VENV/bin/python" -c 'import yaml' 2>/dev/null; then
    python3 -m venv "$VENV"
    "$VENV/bin/python" -m pip install --disable-pip-version-check --quiet \
        "cmakelang==$VERSION" \
        "PyYAML==6.0.3"
fi

actual_version=$("$VENV/bin/cmake-format" --version)
if [ "$actual_version" != "$VERSION" ]; then
    echo "Expected cmake-format $VERSION, found $actual_version" >&2
    exit 1
fi

case "${1:-}" in
    "")
        cmake_format_args="--in-place"
        ;;
    --check)
        cmake_format_args="--check"
        ;;
    *)
        echo "Usage: $0 [--check]" >&2
        exit 2
        ;;
esac

file_list=$(git ls-files -- \
    'CMakeLists.txt' \
    '**/CMakeLists.txt' \
    '*.cmake' \
    '**/*.cmake' \
    | while IFS= read -r path; do
        [ -f "$path" ] && printf '%s\n' "$path"
    done)
if [ -z "$file_list" ]; then
    echo "note: no CMake sources yet; format:cmake is a no-op until Phase 2 lands them"
    exit 0
fi

printf '%s\n' "$file_list" | xargs "$VENV/bin/cmake-format" $cmake_format_args
