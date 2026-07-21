#!/bin/sh
set -eu

case "${1:-}" in
    "")
        clang_format_args="-i"
        ;;
    --check)
        clang_format_args="--dry-run --Werror"
        ;;
    *)
        echo "Usage: $0 [--check]" >&2
        exit 2
        ;;
esac

if [ ! -d packages/tex-core ]; then
    echo "note: no C sources yet; format:c is a no-op until Phase 2 lands packages/tex-core"
    exit 0
fi

# Generated headers (tex-core-export.h, tex-core-version.h) are configured
# into the build tree, never the source tree, so no exclusions are needed;
# the committed generated metrics table is metrics.inc, which the patterns
# below do not match.
file_list=$(find packages/tex-core -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) \
    -print)
if [ -z "$file_list" ]; then
    echo "note: no C sources yet; format:c is a no-op until Phase 2 lands them"
    exit 0
fi

EXPECTED_VERSION="22.1.8"
REPO_CLANG_FORMAT="$PWD/.tools/clang-format/$EXPECTED_VERSION/venv/bin/clang-format"
if [ -n "${CLANG_FORMAT:-}" ]; then
    :
elif [ -x "$REPO_CLANG_FORMAT" ]; then
    CLANG_FORMAT=$REPO_CLANG_FORMAT
else
    CLANG_FORMAT=clang-format
fi

actual_version=$($CLANG_FORMAT --version | sed -E 's/.*version ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')
if [ "$actual_version" != "$EXPECTED_VERSION" ]; then
    echo "Expected clang-format $EXPECTED_VERSION, found $actual_version" >&2
    exit 1
fi

printf '%s\n' "$file_list" | xargs "$CLANG_FORMAT" $clang_format_args
