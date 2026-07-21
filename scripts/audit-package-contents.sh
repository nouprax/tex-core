#!/bin/sh
# Installed and published package-content audit.
#
# Phase A scope: the C archive only — install both library shapes into
# throwaway prefixes, enforce a product-only artifact allowlist, and prove
# the installed package is consumable through pkg-config and CMake
# find_package link consumers. Later phases extend this audit to the Swift
# source archive, the npm tarball, and every Maven publication.
set -eu

temp_dir=$(mktemp -d)
trap 'rm -rf "$temp_dir"' EXIT

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "pkg-config is required for installed C consumer validation" >&2
    exit 1
fi

cmake -S . -B "$temp_dir/cmake-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTEX_CORE_TESTS=OFF \
    -DTEX_CORE_STATIC=OFF \
    -DTEX_CORE_SHARED=ON \
    -DCMAKE_INSTALL_PREFIX="$temp_dir/cmake-prefix" >/dev/null
cmake --build "$temp_dir/cmake-build" --parallel 2 >/dev/null
cmake --install "$temp_dir/cmake-build" >/dev/null

cmake -S . -B "$temp_dir/cmake-build-static" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTEX_CORE_TESTS=ON \
    -DTEX_CORE_STATIC=ON \
    -DTEX_CORE_SHARED=OFF \
    -DCMAKE_INSTALL_PREFIX="$temp_dir/cmake-prefix-static" >/dev/null
cmake --build "$temp_dir/cmake-build-static" --parallel 2 >/dev/null
cmake --install "$temp_dir/cmake-build-static" >/dev/null

# The install step must not drag test or spec support into the prefix even
# when the build tree was configured with tests on.
find "$temp_dir/cmake-prefix" "$temp_dir/cmake-prefix-static" \( -type f -o -type l \) |
    while IFS= read -r artifact; do
        case "$artifact" in
            *test* | *Test* | *specs* | *render-tree*)
                echo "Test or spec artifact leaked into the C install prefix: $artifact" >&2
                exit 1
                ;;
        esac
    done

find "$temp_dir/cmake-prefix" \( -type f -o -type l \) | while IFS= read -r artifact; do
    relative_path=${artifact#"$temp_dir/cmake-prefix/"}
    case "$relative_path" in
        bin/tex-core | \
            include/tex_core.h | \
            include/tex-core-export.h | \
            include/tex-core-version.h | \
            lib/cmake/tex-core/tex-core-config.cmake | \
            lib/cmake/tex-core/tex-core-config-version.cmake | \
            lib/cmake/tex-core/tex-core-targets.cmake | \
            lib/cmake/tex-core/tex-core-targets-release.cmake | \
            lib/libtex-core* | \
            lib/pkgconfig/tex-core.pc) ;;
        *)
            echo "Unexpected C install artifact: $relative_path" >&2
            exit 1
            ;;
    esac
done

find "$temp_dir/cmake-prefix-static" \( -type f -o -type l \) | while IFS= read -r artifact; do
    relative_path=${artifact#"$temp_dir/cmake-prefix-static/"}
    case "$relative_path" in
        bin/tex-core | \
            include/tex_core.h | \
            include/tex-core-export.h | \
            include/tex-core-version.h | \
            lib/cmake/tex-core/tex-core-config.cmake | \
            lib/cmake/tex-core/tex-core-config-version.cmake | \
            lib/cmake/tex-core/tex-core-targets.cmake | \
            lib/cmake/tex-core/tex-core-targets-release.cmake | \
            lib/libtex-core.a | \
            lib/pkgconfig/tex-core.pc) ;;
        *)
            echo "Unexpected static C install artifact: $relative_path" >&2
            exit 1
            ;;
    esac
done

# The one exported CMake target is tex-core::tex-core; internal target
# names must not surface in installed metadata.
for prefix in "$temp_dir/cmake-prefix" "$temp_dir/cmake-prefix-static"; do
    if ! grep -R -I -q 'tex-core::tex-core' "$prefix/lib/cmake/tex-core"; then
        echo "Installed CMake metadata does not export tex-core::tex-core: $prefix" >&2
        exit 1
    fi
    if grep -R -I -n 'tex-core::libtex-core' "$prefix/lib/cmake/tex-core"; then
        echo "Internal library target names leaked into installed CMake metadata" >&2
        exit 1
    fi
done

# pkg-config intentionally returns separate compiler/linker arguments.
# shellcheck disable=SC2046
PKG_CONFIG_PATH="$temp_dir/cmake-prefix/lib/pkgconfig" \
    cc packages/tex-core/tests/consumers/c/main.c \
    -o "$temp_dir/pkg-config-consumer-shared" \
    $(PKG_CONFIG_PATH="$temp_dir/cmake-prefix/lib/pkgconfig" \
        pkg-config --cflags --libs tex-core)
DYLD_LIBRARY_PATH="$temp_dir/cmake-prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
    LD_LIBRARY_PATH="$temp_dir/cmake-prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$temp_dir/pkg-config-consumer-shared"

# shellcheck disable=SC2046
PKG_CONFIG_PATH="$temp_dir/cmake-prefix-static/lib/pkgconfig" \
    cc packages/tex-core/tests/consumers/c/main.c \
    -o "$temp_dir/pkg-config-consumer-static" \
    $(PKG_CONFIG_PATH="$temp_dir/cmake-prefix-static/lib/pkgconfig" \
        pkg-config --static --cflags --libs tex-core)
"$temp_dir/pkg-config-consumer-static"

cmake -S packages/tex-core/tests/consumers/cmake \
    -B "$temp_dir/cmake-consumer-shared" \
    -DCMAKE_PREFIX_PATH="$temp_dir/cmake-prefix" >/dev/null
cmake --build "$temp_dir/cmake-consumer-shared" --parallel 2 >/dev/null
DYLD_LIBRARY_PATH="$temp_dir/cmake-prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
    LD_LIBRARY_PATH="$temp_dir/cmake-prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$temp_dir/cmake-consumer-shared/tex-core-installed-consumer"

cmake -S packages/tex-core/tests/consumers/cmake \
    -B "$temp_dir/cmake-consumer-static" \
    -DCMAKE_PREFIX_PATH="$temp_dir/cmake-prefix-static" >/dev/null
cmake --build "$temp_dir/cmake-consumer-static" --parallel 2 >/dev/null
"$temp_dir/cmake-consumer-static/tex-core-installed-consumer"

echo "package contents audit passed"
